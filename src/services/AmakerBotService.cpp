/**
 * @file AmakerBotService.cpp
 * @brief Token-based master-registration and heartbeat-watchdog service.
 *
 * @details All master-IP state lives in BotMaster (injected at construction).
 *          AmakerBotService only owns: the one-time token, the bot name, and
 *          the heartbeat watchdog.
 */

#include "services/AmakerBotService.h"
#include "RollingLogger.h"
#include "FlashStringHelper.h"
#include <Arduino.h>          // millis(), esp_random()
#include <LittleFS.h>         // listScripts / getScript / saveScript / deleteScript
#include <esp_efuse.h>        // esp_efuse_mac_get_default()

// ---------------------------------------------------------------------------
// PROGMEM constants (translation-unit local)
// ---------------------------------------------------------------------------

namespace AmakerBotConsts
{   
    constexpr const char str_service_name[]     PROGMEM = "AmakerBot Service";
    constexpr const char default_bot_name[]     PROGMEM = "K10-Bot";
    constexpr const char msg_registered[]          PROGMEM = "Master registered: ";
    constexpr const char msg_already_registered[]  PROGMEM = "Master already registered: ";
    constexpr const char msg_unregistered[]        PROGMEM = "Master unregistered: ";
    constexpr const char msg_unregister_denied[]   PROGMEM = "Unregister denied (not master): ";


    constexpr const char msg_token[]            PROGMEM = "AmakerBot: token=";
    constexpr const char msg_master_set[]       PROGMEM = "AmakerBot: master registered: ";
    constexpr const char msg_master_cleared[]   PROGMEM = "AmakerBot: master unregistered";
    constexpr const char msg_name_changed[]     PROGMEM = "AmakerBot: bot name=";
    constexpr const char msg_hb_timeout[]       PROGMEM = "AmakerBot: heartbeat timeout";
    constexpr const char msg_hb_restored[]      PROGMEM = "AmakerBot: heartbeat restored";
    constexpr const char msg_master_needed[]    PROGMEM = "AmakerBot: BotMaster must be started first";
    constexpr const char msg_mutex_failed[]     PROGMEM = "AmakerBot: name_mutex alloc failed";
    constexpr const char msg_wifi_not_set[]     PROGMEM = "AmakerBot: WiFi service not injected";
    constexpr const char msg_wifi_settings_set[] PROGMEM = "AmakerBot: WiFi credentials updated by master";
    constexpr const char msg_wifi_settings_reset[] PROGMEM = "AmakerBot: WiFi settings reset to defaults by master";
    constexpr const char msg_reboot_requested[] PROGMEM = "AmakerBot: emergency reboot requested by master";

    constexpr uint8_t  BOT_SERVICE_ID      = 0x04;
    constexpr uint8_t  CMD_REGISTER        = 0x01; ///< [token bytes…]
    constexpr uint8_t  CMD_UNREGISTER      = 0x02; ///< (no payload)
    constexpr uint8_t  CMD_HEARTBEAT       = 0x03; ///< (no payload, no reply)
    constexpr uint8_t  CMD_PING            = 0x04; ///< [id:4B] → echo
    constexpr uint8_t  CMD_GET_NAME        = 0x05; ///< → [resp_ok][name…]
    constexpr uint8_t  CMD_SET_NAME        = 0x06; ///< [name…] (master only)
    constexpr uint8_t  CMD_GET_WIFI        = 0x07; ///< → [resp_ok][ssid_len:1B][ssid…][pass_len:1B][pass…]
    constexpr uint8_t  CMD_SET_WIFI        = 0x08; ///< [ssid_len:1B][ssid…][pass_len:1B][pass…] master-only; saves+applies
    constexpr uint8_t  CMD_RESET_WIFI      = 0x09; ///< (no payload) master-only; clears NVS, restores defaults
    constexpr uint8_t  CMD_REBOOT          = 0x0A; ///< (no payload) master-only; emergency reboot, no reply

    constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 50;  ///< ms without heartbeat → emergency stop

    // ---- Script filesystem constants ----------------------------------------
    constexpr const char scripts_dir[]             PROGMEM = "/scripts";
    constexpr const char scripts_dir_alt[]         PROGMEM = "/data/scripts";
    constexpr const char scripts_index[]           PROGMEM = "/scripts/index.txt";
    constexpr const char scripts_index_alt[]       PROGMEM = "/data/scripts/index.txt";
    constexpr const char msg_script_saved[]        PROGMEM = "AmakerBot: script saved: ";
    constexpr const char msg_script_deleted[]      PROGMEM = "AmakerBot: script deleted: ";
    constexpr const char msg_script_not_found[]    PROGMEM = "AmakerBot: script not found: ";
    constexpr const char msg_script_name_invalid[] PROGMEM = "AmakerBot: invalid script name";
    constexpr const char msg_scripts_dir_created[] PROGMEM = "AmakerBot: /scripts dir created";
} // namespace AmakerBotConsts

char svccode[6] = "proxy";
// ---------------------------------------------------------------------------
// IsServiceInterface
// ---------------------------------------------------------------------------

std::string AmakerBotService::getServiceName()
{
    return progmem_to_string(AmakerBotConsts::str_service_name);
}

bool AmakerBotService::initializeService()
{   
    if (isServiceInitialized())
        return true;


    name_mutex_ = xSemaphoreCreateMutex();
    if (!name_mutex_)
    {
        if (debugLogger)
            debugLogger->error(FPSTR(AmakerBotConsts::msg_mutex_failed));
        setServiceStatus(INITIALIZED_FAILED);
        return false;
    }
    master_mutex_ = xSemaphoreCreateMutex();
    if (!master_mutex_)
    {
        if (debugLogger)
            debugLogger->error(progmem_to_string(AmakerBotConsts::msg_mutex_failed).c_str());
        setServiceStatus(INITIALIZED_FAILED);
        return false;
    }
    server_token_  = generateRandomToken();
    bot_name_      = progmem_to_string(AmakerBotConsts::default_bot_name);

    heartbeat_active_    = false;
    heartbeat_timed_out_ = false;

    if (debugLogger)
        debugLogger->info(progmem_to_string(AmakerBotConsts::msg_token) + server_token_);
    master_ip_    = "";
    last_seen_ms_ = 0;

    setServiceStatus(INITIALIZED);

    if (serviceLogger) serviceLogger->info((getServiceName() + " " + FPSTR(ServiceConst::msg_init_ok)).c_str(),svccode);
    return true;
}

bool AmakerBotService::startService()
{
    if (!isServiceInitialized())
    {
        setServiceStatus(START_FAILED);
        return false;
    }

    setServiceStatus(STARTED);
    serviceLogger->info((getServiceName() + " " + FPSTR(ServiceConst::msg_start_ok)).c_str(),svccode);
    return true;
}

bool AmakerBotService::stopService()
{
    if (name_mutex_)
    {
        vSemaphoreDelete(name_mutex_);
        name_mutex_ = nullptr;
    }
     if (master_mutex_)
    {
        vSemaphoreDelete(master_mutex_);
        master_mutex_ = nullptr;
    }
    setServiceStatus(STOPPED);
    return true;
}

// ---------------------------------------------------------------------------
// IsBotActionHandlerInterface
// ---------------------------------------------------------------------------

uint8_t AmakerBotService::getBotServiceId() const
{
    return AmakerBotConsts::BOT_SERVICE_ID;
}

std::string AmakerBotService::handleBotMessage(const uint8_t *data, size_t len,
                                                const std::string &senderIP)
{
    if (!data || len < 1)
        return BotProto::make_ack(0x00, BotProto::resp_invalid_params);

    const uint8_t action = data[0];
    const uint8_t cmd    = BotProto::command(action);

    // ---- CMD_HEARTBEAT 0x03 : keep-alive, no reply --------------------
    if (cmd == AmakerBotConsts::CMD_HEARTBEAT)
    {
        updateMasterLastSeen();
        heartbeat_active_    = true;
        heartbeat_timed_out_ = false;
        return {};  // intentionally no reply to keep latency low
    }

    // ---- CMD_REGISTER 0x01 : [token bytes…] ---------------------------
    if (cmd == AmakerBotConsts::CMD_REGISTER)
    {
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const std::string token(reinterpret_cast<const char *>(data + 1), len - 1);

        if (token != server_token_)
            return BotProto::make_ack(action, BotProto::resp_not_master);

        // Idempotent: already the master → just confirm
        if (isMaster(senderIP))
            return BotProto::make_ack(action, BotProto::resp_ok);

        // Attempt to register; fails only if a *different* master is active
        const bool ok = registerMaster(senderIP);
        if (ok)
        {
            // Reset heartbeat watchdog for the new master session
            heartbeat_active_    = false;
            heartbeat_timed_out_ = false;
        }
        return BotProto::make_ack(action, ok ? BotProto::resp_ok
                                             : BotProto::resp_operation_failed);
    }

    // ---- CMD_UNREGISTER 0x02 : (no payload, sender must be master) ----
    if (cmd == AmakerBotConsts::CMD_UNREGISTER)
    {
        const bool ok = unregister(senderIP);
        if (ok)
            debugLogger->info(FPSTR(AmakerBotConsts::msg_master_cleared));
        return BotProto::make_ack(action, ok ? BotProto::resp_ok : BotProto::resp_not_master);
    }


    // ---- CMD_PING 0x04 : echo 4-byte ID payload -----------------------
    if (cmd == AmakerBotConsts::CMD_PING)
    {
        if (len < 5)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        // Echo: [action][id:4B]
        std::string reply;
        reply.reserve(5);
        reply += static_cast<char>(action);
        reply.append(reinterpret_cast<const char *>(data + 1), 4);
        return reply;
    }

    // ---- CMD_GET_NAME 0x05 : reply with [action][resp_ok][name…] ------
    if (cmd == AmakerBotConsts::CMD_GET_NAME)
    {
        const std::string name = getBotName();
        std::string reply;
        reply.reserve(2 + name.size());
        reply += static_cast<char>(action);
        reply += static_cast<char>(BotProto::resp_ok);
        reply += name;
        return reply;
    }

    // ---- CMD_SET_NAME 0x06 : [name…] (master only) --------------------
    if (cmd == AmakerBotConsts::CMD_SET_NAME)
    {
        if (!isMaster(senderIP))
            return BotProto::make_ack(action, BotProto::resp_not_master);

        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const std::string name(reinterpret_cast<const char *>(data + 1), len - 1);
        if (name.empty() || name.size() > 32)
            return BotProto::make_ack(action, BotProto::resp_invalid_values);

        setBotName(name);
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    // ---- CMD_GET_WIFI 0x07 : → [action][resp_ok][ssid_len:1B][ssid…][pass_len:1B][pass…] ---
    if (cmd == AmakerBotConsts::CMD_GET_WIFI)
    {
        if (!wifi_service_)
        {
            if (debugLogger) debugLogger->error(FPSTR(AmakerBotConsts::msg_wifi_not_set));
            return BotProto::make_ack(action, BotProto::resp_not_started);
        }
        const std::string ssid = wifi_service_->getWifiSsid();
        const std::string pass = wifi_service_->getWifiPassword();
        std::string reply;
        reply.reserve(4 + ssid.size() + pass.size());
        reply += static_cast<char>(action);
        reply += static_cast<char>(BotProto::resp_ok);
        reply += static_cast<char>(ssid.size() & 0xFF);
        reply += ssid;
        reply += static_cast<char>(pass.size() & 0xFF);
        reply += pass;
        return reply;
    }

    // ---- CMD_SET_WIFI 0x08 : [ssid_len:1B][ssid…][pass_len:1B][pass…] (master only) ---
    if (cmd == AmakerBotConsts::CMD_SET_WIFI)
    {
        if (!isMaster(senderIP))
            return BotProto::make_ack(action, BotProto::resp_not_master);
        if (!wifi_service_)
        {
            if (debugLogger) debugLogger->error(FPSTR(AmakerBotConsts::msg_wifi_not_set));
            return BotProto::make_ack(action, BotProto::resp_not_started);
        }
        // Payload: [ssid_len:1B][ssid…][pass_len:1B][pass…]
        if (len < 3)  // need at least action + ssid_len + pass_len
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t ssid_len = data[1];
        if (len < static_cast<size_t>(2 + ssid_len + 1))
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const std::string ssid(reinterpret_cast<const char *>(data + 2), ssid_len);
        const uint8_t pass_len = data[2 + ssid_len];
        if (len < static_cast<size_t>(2 + ssid_len + 1 + pass_len))
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const std::string pass(reinterpret_cast<const char *>(data + 3 + ssid_len), pass_len);
        if (ssid.empty() || ssid.size() > 32 || pass.size() > 64)
            return BotProto::make_ack(action, BotProto::resp_invalid_values);
        wifi_service_->setWifiCredentials(ssid, pass);
        wifi_service_->saveSettings();
        if (debugLogger) debugLogger->info(FPSTR(AmakerBotConsts::msg_wifi_settings_set));
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    // ---- CMD_RESET_WIFI 0x09 : (no payload, master only) --------------
    if (cmd == AmakerBotConsts::CMD_RESET_WIFI)
    {
        if (!isMaster(senderIP))
            return BotProto::make_ack(action, BotProto::resp_not_master);
        if (!wifi_service_)
        {
            if (debugLogger) debugLogger->error(FPSTR(AmakerBotConsts::msg_wifi_not_set));
            return BotProto::make_ack(action, BotProto::resp_not_started);
        }
        wifi_service_->resetSettings();
        if (debugLogger) debugLogger->info(FPSTR(AmakerBotConsts::msg_wifi_settings_reset));
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    // ---- CMD_REBOOT 0x0A : (no payload, master only, no reply) ------
    if (cmd == AmakerBotConsts::CMD_REBOOT)
    {
        if (!isMaster(senderIP))
            return BotProto::make_ack(action, BotProto::resp_not_master);

        reboot();
        return {};
    }

    return BotProto::make_ack(action, BotProto::resp_unknown_cmd);
}

// ---------------------------------------------------------------------------
// Public accessors — all master operations delegate to BotMaster
// ---------------------------------------------------------------------------

bool AmakerBotService::isMaster(const std::string &requesterIP) const
{
if (!master_mutex_)
        return false;

    xSemaphoreTake(master_mutex_, portMAX_DELAY);
    const bool result = (!master_ip_.empty() && master_ip_ == requesterIP);
    xSemaphoreGive(master_mutex_);
    return result;
}

std::string AmakerBotService::getServerToken() const
{
    return server_token_;
}

std::string AmakerBotService::getBotName() const
{
    if (!name_mutex_)
        return progmem_to_string(AmakerBotConsts::default_bot_name);

    xSemaphoreTake(name_mutex_, portMAX_DELAY);
    const std::string name = bot_name_;
    xSemaphoreGive(name_mutex_);
    return name;
}

void AmakerBotService::reboot()
{
    if (debugLogger)
        debugLogger->warning(FPSTR(AmakerBotConsts::msg_reboot_requested));

    if (heartbeat_timeout_cb_)
        heartbeat_timeout_cb_();

    delay(20);
    ESP.restart();
}

void AmakerBotService::setBotName(const std::string &name)
{
    if (name.empty() || name.size() > 32 || !name_mutex_)
        return;

    xSemaphoreTake(name_mutex_, portMAX_DELAY);
    bot_name_ = name;
    xSemaphoreGive(name_mutex_);

    debugLogger->info(progmem_to_string(AmakerBotConsts::msg_name_changed) + name);
}

void AmakerBotService::registerHandler(IsBotActionHandlerInterface *handler)
{
    if (handler)
        bot_message_handlers.push_back(handler);
}

bool AmakerBotService::setMasterIfTokenValid(const std::string &ip, const std::string &token)
{
    if (token != server_token_)
        return false;

    const bool ok = registerMaster(ip);
    if (ok)
    {
        // Reset watchdog for the new master session
        heartbeat_active_    = false;
        heartbeat_timed_out_ = false;
        debugLogger->info(progmem_to_string(AmakerBotConsts::msg_master_set) + ip);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Heartbeat watchdog
// ---------------------------------------------------------------------------

void AmakerBotService::setHeartbeatTimeoutCallback(std::function<void()> cb)
{
    heartbeat_timeout_cb_ = std::move(cb);
}

void AmakerBotService::checkHeartbeatTimeout()
{
    // Only watch if a master is registered and at least one heartbeat arrived
    if (getMasterIP().empty() || !heartbeat_active_)
        return;

    const unsigned long elapsed = millis() - getMasterLastSeen();

    if (elapsed > AmakerBotConsts::HEARTBEAT_TIMEOUT_MS)
    {
        if (!heartbeat_timed_out_)
        {
            heartbeat_timed_out_ = true;
            debugLogger->error(FPSTR(AmakerBotConsts::msg_hb_timeout));
            if (heartbeat_timeout_cb_)
                heartbeat_timeout_cb_();
        }
    }
    else if (heartbeat_timed_out_)
    {
        // Heartbeat restored
        heartbeat_timed_out_ = false;
        debugLogger->info(FPSTR(AmakerBotConsts::msg_hb_restored));
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
std::string AmakerBotService::generateRandomToken()
{
    // Generate 5-char uppercase hex token from ESP32 unique chip ID (lower 20 bits)
    // Uses esp_efuse_mac_get_default() from <esp_efuse.h> for a unique identifier
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    // Combine last 3 bytes into a 24-bit value, take lower 20 bits
    uint32_t chip_id = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    chip_id &= 0xFFFFF;  // 20 bits
    
    char token_buf[6];
    snprintf(token_buf, sizeof(token_buf), "%05X", chip_id);
    return std::string(token_buf);
}




// ---------------------------------------------------------------------------
// AmakerBotService::dispatch
// ---------------------------------------------------------------------------

std::string AmakerBotService::dispatch(const uint8_t *data, size_t len,
                                        const std::string &senderIP)
{
    if (!data || len == 0)
    {
        if (debugLogger)
            debugLogger->debug(FPSTR(BotMessageHandlerConsts::msg_empty_message));
        return {};
    }
    
    ++dispatch_count_[data[0]]; // count every call by action byte
    if ((data[0]>>4) == getBotServiceId())
        return handleBotMessage(data, len, senderIP);
    else {


        
    }
    for (IsBotActionHandlerInterface *handler : bot_message_handlers) {
        if (handler && (data[0]>>4) == handler->getBotServiceId()) {
            if (serviceLogger &&
                serviceLogger->get_log_level() >= RollingLogger::DEBUG)
            {
                std::string hex_data;
                hex_data.reserve(len * 2);
                for (size_t i = 0; i < len; ++i) {
                    char buf[3];
                    snprintf(buf, sizeof(buf), "%02X", data[i]);
                    hex_data += buf;
                }
                serviceLogger->debug(("RX " + hex_data).c_str(), svccode);
            }
            return handler->handleBotMessage(data, len);
        }
    }
    return BotProto::make_ack(data[0], BotProto::resp_unknown_service);
}


//------------------------------------------------------------------
// Master registry
// ---------------------------------------------------------------------------

bool AmakerBotService::registerMaster(const std::string &requesterIP)
{
    if (!master_mutex_)
        return false;

    xSemaphoreTake(master_mutex_, portMAX_DELAY);
    const bool already_set = !master_ip_.empty();
    if (!already_set)
        master_ip_ = requesterIP;
    xSemaphoreGive(master_mutex_);
    if (debugLogger)
    {
        if (already_set)
            debugLogger->info((progmem_to_string(AmakerBotConsts::msg_already_registered) + requesterIP).c_str());
        else
            debugLogger->info((progmem_to_string(AmakerBotConsts::msg_registered) + requesterIP).c_str());
    }
    return !already_set;
}

bool AmakerBotService::unregister(const std::string &requesterIP)
{
    if (!master_mutex_)
        return false;

    xSemaphoreTake(master_mutex_, portMAX_DELAY);
    const bool is_master = (master_ip_ == requesterIP);
    if (is_master)
        master_ip_ = "";
    xSemaphoreGive(master_mutex_);
    if (debugLogger)
    {
        if (is_master)
            debugLogger->info((progmem_to_string(AmakerBotConsts::msg_unregistered) + requesterIP).c_str());
        else
            debugLogger->info((progmem_to_string(AmakerBotConsts::msg_unregister_denied) + requesterIP).c_str());
    }
    return is_master;
}


std::string AmakerBotService::getMasterIP() const
{
    if (!master_mutex_)
        return "";

    xSemaphoreTake(master_mutex_, portMAX_DELAY);
    const std::string ip = master_ip_;
    xSemaphoreGive(master_mutex_);
    return ip;
}

void AmakerBotService::updateMasterLastSeen()
{
    last_seen_ms_ = millis();
}

unsigned long AmakerBotService::getMasterLastSeen() const
{
    return last_seen_ms_;
}

// ---------------------------------------------------------------------------
// Script filesystem operations
// ---------------------------------------------------------------------------

bool AmakerBotService::isValidScriptName(const std::string &name)
{
    if (name.empty() || name.size() > 64)
        return false;
    if (name.find("..") != std::string::npos)
        return false;
    for (const char c : name)
    {
        if (c == '/' || c == '\\' || c == '\0' || c == ':')
            return false;
    }
    return true;
}

std::vector<std::string> AmakerBotService::listScripts()
{
    std::vector<std::string> names;

    if (debugLogger)
        debugLogger->info("AmakerBot:listScripts begin");

    auto append_entry_name = [&names](const std::string &entry_name,
                                      const char *prefix_to_strip)
    {
        std::string fname = entry_name;
        if (prefix_to_strip)
        {
            if (fname.rfind(prefix_to_strip, 0) != 0)
                return;
            fname = fname.substr(std::char_traits<char>::length(prefix_to_strip));
        }

        const size_t slash = fname.rfind('/');
        if (slash != std::string::npos)
            fname = fname.substr(slash + 1);

        if (fname.empty())
            return;
        if (fname.size() >= 3 && fname.rfind(".gz") == (fname.size() - 3))
            return;

        for (const std::string &existing : names)
        {
            if (existing == fname)
                return;
        }
        names.push_back(fname);
    };

    auto append_from_dir = [&append_entry_name, this](const char *dir_path)
    {
        File dir = LittleFS.open(dir_path);
        if (!dir || !dir.isDirectory())
        {
            if (debugLogger)
                debugLogger->info((std::string("AmakerBot:listScripts open failed or not dir: ") + dir_path).c_str());
            if (dir)
                dir.close();
            return;
        }

        if (debugLogger)
            debugLogger->info((std::string("AmakerBot:listScripts enumerate dir: ") + dir_path).c_str());

    {
        File entry = dir.openNextFile();
        while (entry)
        {
            if (!entry.isDirectory())
            {
                    append_entry_name(entry.name(), nullptr);
            }
            entry.close();
            entry = dir.openNextFile();
        }
        }
        dir.close();
    };

    auto append_from_manifest = [&append_entry_name, this](const char *index_path)
    {
        File idx = LittleFS.open(index_path, "r");
        if (!idx || idx.isDirectory())
        {
            if (idx)
                idx.close();
            if (debugLogger)
                debugLogger->info((std::string("AmakerBot:listScripts manifest missing: ") + index_path).c_str());
            return;
        }

        if (debugLogger)
            debugLogger->info((std::string("AmakerBot:listScripts read manifest: ") + index_path).c_str());

        std::string line;
        line.reserve(96);
        while (idx.available())
        {
            const int ch = idx.read();
            if (ch < 0)
                break;
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                if (!line.empty())
                    append_entry_name(line, nullptr);
                line.clear();
                continue;
            }
            line.push_back(static_cast<char>(ch));
        }
        if (!line.empty())
            append_entry_name(line, nullptr);
        idx.close();
    };

    // Deterministic path: read generated index first.
    append_from_manifest(AmakerBotConsts::scripts_index);
    append_from_manifest(AmakerBotConsts::scripts_index_alt);
    if (!names.empty())
    {
        if (debugLogger)
            debugLogger->info((std::string("AmakerBot:listScripts manifest-count=") +
                               std::to_string(names.size())).c_str());
        return names;
    }

    // Prefer direct directory iteration when available.
    if (debugLogger)
        debugLogger->info((std::string("AmakerBot:listScripts probe ") + AmakerBotConsts::scripts_dir +
                           " exists=" + (LittleFS.exists(AmakerBotConsts::scripts_dir) ? "1" : "0")).c_str());
    append_from_dir(AmakerBotConsts::scripts_dir);
    append_from_dir("/scripts/");

    if (debugLogger)
        debugLogger->info((std::string("AmakerBot:listScripts probe ") + AmakerBotConsts::scripts_dir_alt +
                           " exists=" + (LittleFS.exists(AmakerBotConsts::scripts_dir_alt) ? "1" : "0")).c_str());
    append_from_dir(AmakerBotConsts::scripts_dir_alt);
    append_from_dir("/data/scripts/");

    if (debugLogger)
        debugLogger->info((std::string("AmakerBot:listScripts direct-count=") +
                           std::to_string(names.size())).c_str());

    if (!names.empty())
    {
        if (debugLogger)
            debugLogger->info("AmakerBot:listScripts done (direct scan)");
        return names;
    }

    // Fallback for FS variants where directory iteration is limited.
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory())
    {
        if (debugLogger)
            debugLogger->error("AmakerBot:listScripts root open failed");
        return names;
    }

    File entry = root.openNextFile();
    while (entry)
    {
        const std::string full = entry.name();
        if (debugLogger)
            debugLogger->info((std::string("AmakerBot:listScripts root entry=") +
                               full + " dir=" + (entry.isDirectory() ? "1" : "0")).c_str());
        if (!entry.isDirectory())
        {
            append_entry_name(full, "/scripts/");
            append_entry_name(full, "scripts/");
            append_entry_name(full, "/data/scripts/");
            append_entry_name(full, "data/scripts/");
        }
        else
        {
            // Some cores expose directories via root iteration but do not
            // enumerate when reopened by path. Enumerate via the discovered handle.
            if (full == "/scripts" || full == "/scripts/")
            {
                if (debugLogger)
                    debugLogger->info("AmakerBot:listScripts fallback enumerate root entry /scripts");

                File script_entry = entry.openNextFile();
                while (script_entry)
                {
                    if (!script_entry.isDirectory())
                        append_entry_name(script_entry.name(), "/scripts/");
                    script_entry.close();
                    script_entry = entry.openNextFile();
                }
            }
            else if (full == "/data" || full == "/data/")
            {
                // Try nested /data/scripts if present.
                File data_entry = entry.openNextFile();
                while (data_entry)
                {
                    const std::string data_name = data_entry.name();
                    if (data_entry.isDirectory() && (data_name == "/data/scripts" || data_name == "/data/scripts/"))
                    {
                        if (debugLogger)
                            debugLogger->info("AmakerBot:listScripts fallback enumerate root entry /data/scripts");

                        File script_entry = data_entry.openNextFile();
                        while (script_entry)
                        {
                            if (!script_entry.isDirectory())
                                append_entry_name(script_entry.name(), "/data/scripts/");
                            script_entry.close();
                            script_entry = data_entry.openNextFile();
                        }
                    }
                    data_entry.close();
                    data_entry = entry.openNextFile();
                }
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    if (debugLogger)
    {
        debugLogger->info((std::string("AmakerBot:listScripts fallback-count=") +
                           std::to_string(names.size())).c_str());
        for (const std::string &n : names)
            debugLogger->info((std::string("AmakerBot:listScripts file=") + n).c_str());
    }

    return names;
}

std::string AmakerBotService::getScript(const std::string &name)
{
    if (!isValidScriptName(name))
    {
        if (debugLogger)
            debugLogger->error(FPSTR(AmakerBotConsts::msg_script_name_invalid));
        return {};
    }

    const std::string path = std::string(AmakerBotConsts::scripts_dir) + "/" + name;
    const std::string alt_path = std::string(AmakerBotConsts::scripts_dir_alt) + "/" + name;
    if (debugLogger)
        debugLogger->info(("AmakerBot:getScript try " + path).c_str());
    File f = LittleFS.open(path.c_str(), "r");
    if (!f || f.isDirectory())
    {
        if (f)
            f.close();
        if (debugLogger)
            debugLogger->info(("AmakerBot:getScript fallback try " + alt_path).c_str());
        f = LittleFS.open(alt_path.c_str(), "r");
    }
    if (!f || f.isDirectory())
    {
        if (debugLogger)
            debugLogger->error((progmem_to_string(AmakerBotConsts::msg_script_not_found) + name).c_str());
        return {};
    }

    std::string content;
    content.reserve(f.size());
    uint8_t buf[128];
    while (f.available())
    {
        const int n = f.read(buf, sizeof(buf));
        if (n > 0)
            content.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
    }
    f.close();
    return content;
}

bool AmakerBotService::saveScript(const std::string &name, const std::string &content)
{
    if (!isValidScriptName(name))
    {
        if (debugLogger)
            debugLogger->error(FPSTR(AmakerBotConsts::msg_script_name_invalid));
        return false;
    }

    if (!LittleFS.exists(AmakerBotConsts::scripts_dir))
    {
        LittleFS.mkdir(AmakerBotConsts::scripts_dir);
        if (debugLogger)
            debugLogger->info(FPSTR(AmakerBotConsts::msg_scripts_dir_created));
    }

    const std::string path = std::string(AmakerBotConsts::scripts_dir) + "/" + name;
    File f = LittleFS.open(path.c_str(), "w");
    if (!f)
    {
        if (debugLogger)
            debugLogger->error(("AmakerBot: cannot open for write: " + path).c_str());
        return false;
    }

    f.print(content.c_str());
    f.close();

    if (debugLogger)
        debugLogger->info((progmem_to_string(AmakerBotConsts::msg_script_saved) + name).c_str());
    return true;
}

bool AmakerBotService::deleteScript(const std::string &name)
{
    if (!isValidScriptName(name))
    {
        if (debugLogger)
            debugLogger->error(FPSTR(AmakerBotConsts::msg_script_name_invalid));
        return false;
    }

    const std::string path = std::string(AmakerBotConsts::scripts_dir) + "/" + name;
    if (!LittleFS.exists(path.c_str()))
    {
        if (debugLogger)
            debugLogger->error((progmem_to_string(AmakerBotConsts::msg_script_not_found) + name).c_str());
        return false;
    }

    const bool ok = LittleFS.remove(path.c_str());
    if (debugLogger)
    {
        if (ok)
            debugLogger->info((progmem_to_string(AmakerBotConsts::msg_script_deleted) + name).c_str());
        else
            debugLogger->error(("AmakerBot: delete failed: " + path).c_str());
    }
    return ok;
}

bool AmakerBotService::scriptExists(const std::string &name)
{
    if (!isValidScriptName(name))
        return false;
    const std::string path = std::string(AmakerBotConsts::scripts_dir) + "/" + name;
    if (LittleFS.exists(path.c_str()))
    {
        if (debugLogger)
            debugLogger->info(("AmakerBot:scriptExists hit " + path).c_str());
        return true;
    }
    const std::string alt_path = std::string(AmakerBotConsts::scripts_dir_alt) + "/" + name;
    const bool found_alt = LittleFS.exists(alt_path.c_str());
    if (debugLogger && found_alt)
        debugLogger->info(("AmakerBot:scriptExists hit " + alt_path).c_str());
    return found_alt;
}
