/**
 * @file BotServerWeb.cpp
 * @brief Implementation of BotServerWeb — thin HTTP transport for the bot protocol.
 */

#include "BotCommunication/BotServerWeb.h"
#include "RollingLogger.h"
#include "FlashStringHelper.h"
#include "generated/BuildInfo.h"
#include <Arduino.h>        // FPSTR()
#include <esp_camera.h>
#include <img_converters.h> // frame2jpg()
#include <memory>

namespace
{
namespace StaticRouteConsts
{
    constexpr const char path_botscript_html[] PROGMEM = "/BotScript.html";
    constexpr const char path_botscript_js[] PROGMEM = "/BotScript.js";
    constexpr const char path_botscript_actions_js[] PROGMEM = "/BotScriptActions.js";
    constexpr const char path_common_js[] PROGMEM = "/common.js";
    constexpr const char path_amaker_css[] PROGMEM = "/amaker.css";
    constexpr const char path_ws_joystick_css[] PROGMEM = "/WebSocketJoystick.css";
    constexpr const char path_botscript_bundle_css[] PROGMEM = "/BotScript.bundle.css";
    constexpr const char path_botscript_bundle_js[] PROGMEM = "/BotScript.bundle.js";

    constexpr const char fs_botscript_html[] PROGMEM = "/www/BotScript.html";
    constexpr const char fs_botscript_js[] PROGMEM = "/www/BotScript.js";
    constexpr const char fs_botscript_actions_js[] PROGMEM = "/www/BotScriptActions.js";
    constexpr const char fs_common_js[] PROGMEM = "/www/common.js";
    constexpr const char fs_amaker_css[] PROGMEM = "/www/amaker.css";
    constexpr const char fs_ws_joystick_css[] PROGMEM = "/www/WebSocketJoystick.css";
    constexpr const char fs_botscript_bundle_css[] PROGMEM = "/www/BotScript.bundle.css";
    constexpr const char fs_botscript_bundle_js[] PROGMEM = "/www/BotScript.bundle.js";

    constexpr const char mime_css[] PROGMEM = "text/css; charset=utf-8";
    constexpr const char mime_js[] PROGMEM = "application/javascript; charset=utf-8";
}

/**
 * @brief Escape a string so it is safe to embed as a JSON string value.
 * @param input Source string.
 * @return Escaped JSON string without surrounding quotes.
 */
std::string jsonEscape(const std::string &input)
{
    std::string out;
    out.reserve(input.size() + 8);

    for (const unsigned char c : input)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
                break;
        }
    }

    return out;
}

/**
 * @brief Append a JSON string field to an existing object payload.
 * @param json Destination JSON string.
 * @param key Field name.
 * @param value Field value.
 * @param with_comma True to append a trailing comma after the field.
 */
void appendJsonStringField(std::string &json,
                           const char *key,
                           const std::string &value,
                           bool with_comma = true)
{
    json += '"';
    json += key;
    json += "\":\"";
    json += jsonEscape(value);
    json += '"';
    if (with_comma)
        json += ',';
}

/**
 * @brief Build a streaming response that owns payload storage via shared_ptr.
 *
 * This avoids use-after-free in async send paths by keeping payload bytes alive
 * for the lifetime of the response callback.
 */
AsyncWebServerResponse *beginOwnedResponse(AsyncWebServerRequest *request,
                                           const char *mime,
                                           const std::shared_ptr<std::string> &payload)
{
    return request->beginResponse(
        mime,
        payload->size(),
        [payload](uint8_t *buffer, size_t maxLen, size_t index) -> size_t
        {
            if (index >= payload->size())
                return 0;

            const size_t remaining = payload->size() - index;
            const size_t to_copy = (remaining < maxLen) ? remaining : maxLen;
            memcpy(buffer, payload->data() + index, to_copy);
            return to_copy;
        });
}

/**
 * @brief Log a received HTTP request in a compact form for diagnostics.
 */
void logHttpRequest(RollingLogger *logger, AsyncWebServerRequest *request)
{
    if (!logger || !request)
        return;

    std::string msg("HTTP ");
    msg += request->methodToString();
    msg += ' ';
    msg += request->url().c_str();
    msg += " from ";
    msg += request->client()->remoteIP().toString().c_str();
    logger->info(msg, "HTTP ");
}

/**
 * @brief Register a GET static-file route with lightweight open/size logging.
 */
void registerLoggedStaticRoute(AsyncWebServer *server,
                               RollingLogger *logger,
                               const char *url_path,
                               const char *fs_path,
                               const char *mime)
{
    server->on(url_path, HTTP_GET,
        [logger, fs_path, mime](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger, request);

            std::string selected_path(fs_path);
            bool use_gzip = false;

            if (request->hasHeader("Accept-Encoding"))
            {
                const AsyncWebHeader *accept_header = request->getHeader("Accept-Encoding");
                if (accept_header && accept_header->value().indexOf("gzip") >= 0)
                {
                    const std::string gz_path = selected_path + ".gz";
                    if (LittleFS.exists(gz_path.c_str()))
                    {
                        selected_path = gz_path;
                        use_gzip = true;
                    }
                }
            }

            File file = LittleFS.open(selected_path.c_str(), "r");
            if (!file)
            {
                if (logger)
                    logger->warning(std::string("HTTP static open failed ") + selected_path,
                                    "HTTP ");
                request->send(404, FPSTR(BotServerWebConsts::mime_text), "Not found");
                return;
            }

            const size_t size = file.size();

            if (logger)
            {
                logger->info(std::string("HTTP static ")
                             + request->url().c_str()
                             + " size="
                             + std::to_string(static_cast<unsigned long>(size))
                             + (use_gzip ? " gz=1" : " gz=0"),
                             "HTTP ");
            }

            file.seek(0, SeekSet);

            auto payload = std::make_shared<std::string>();
            payload->resize(size);

            size_t bytes_read = 0;
            while (bytes_read < size)
            {
                const size_t remaining = size - bytes_read;
                const size_t read_now = file.read(
                    reinterpret_cast<uint8_t *>(payload->data() + bytes_read),
                    remaining);
                if (read_now == 0)
                    break;
                bytes_read += read_now;
            }
            file.close();

            if (bytes_read != size)
            {
                if (logger)
                    logger->warning(std::string("HTTP static short read ")
                                    + selected_path
                                    + " got="
                                    + std::to_string(static_cast<unsigned long>(bytes_read))
                                    + " want="
                                    + std::to_string(static_cast<unsigned long>(size)),
                                    "HTTP ");
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "Read failed");
                return;
            }

            AsyncWebServerResponse *response = beginOwnedResponse(
                request,
                mime,
                payload);
            if (!response)
            {
                if (logger)
                    logger->warning(std::string("HTTP static response alloc failed ") + selected_path,
                                    "HTTP ");
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "OOM");
                return;
            }

            if (use_gzip)
                response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Connection", "close");
            response->addHeader("Cache-Control", "no-store");
            request->send(response);
        });
}
} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BotServerWeb::BotServerWeb(AmakerBotService &bot, uint16_t port)
    : bot_(bot), port_(port)
{
}

// ---------------------------------------------------------------------------
// hexDecode
// ---------------------------------------------------------------------------

bool BotServerWeb::hexDecode(const char *hex, size_t hex_len, std::string &out)
{
    if (!hex || hex_len == 0 || hex_len % 2 != 0)
        return false;

    out.clear();
    out.reserve(hex_len / 2);

    for (size_t i = 0; i < hex_len; i += 2)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return false;

        out += static_cast<char>((hi << 4) | lo);
    }
    return true;
}

// ---------------------------------------------------------------------------
// hexEncode
// ---------------------------------------------------------------------------

std::string BotServerWeb::hexEncode(const uint8_t *data, size_t len)
{
    static constexpr char hex_chars[] = "0123456789abcdef";

    std::string out;
    out.reserve(len * 2);

    for (size_t i = 0; i < len; ++i)
    {
        out += hex_chars[data[i] >> 4];
        out += hex_chars[data[i] & 0x0F];
    }
    return out;
}
// ---------------------------------------------------------------------------

void BotServerWeb::register_get_botserver()
{
    // Capture `this` — BotServerWeb is long-lived (typically static in main.cpp)
    server_->on(BotServerWebConsts::path_botserver, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            // ---- Validate `cmd` parameter ----
            if (!request->hasParam(FPSTR(BotServerWebConsts::param_cmd)))
            {
                ++dropped_count_;
                request->send(400, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_missing_cmd));
                return;
            }

            const String &hex_str = request->getParam(
                FPSTR(BotServerWebConsts::param_cmd))->value();

            // ---- Hex-decode the binary frame ----
            std::string frame;
            if (!hexDecode(hex_str.c_str(), hex_str.length(), frame))
            {
                ++dropped_count_;
                request->send(400, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_invalid_hex));
                return;
            }

            ++rx_count_;

            // ---- Dispatch — lock-free, synchronous ----
            const std::string response = bot_.dispatch(
                reinterpret_cast<const uint8_t *>(frame.data()), frame.size());

            // ---- Reply ----
            if (response.empty())
            {
                // Handler processed the frame but has nothing to say
                request->send(204);
                return;
            }

            ++tx_count_;
            auto payload = std::make_shared<std::string>(response);
            AsyncWebServerResponse *resp = beginOwnedResponse(
                request,
                reinterpret_cast<const char *>(FPSTR(BotServerWebConsts::mime_octet)),
                payload);
            if (!resp)
            {
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "OOM");
                return;
            }
            resp->addHeader("Cache-Control", "no-store");
            request->send(resp);
        });
}

// ---------------------------------------------------------------------------
// registerBuildInfoRoute
// ---------------------------------------------------------------------------

void BotServerWeb::registerBuildInfoRoute()
{
    server_->on(BotServerWebConsts::path_buildinfo_api, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            std::string json;
            json.reserve(768);

            json += '{';
            json += "\"schema_version\":";
            json += std::to_string(BuildInfoConsts::schema_version);
            json += ",\"firmware\":{";
            appendJsonStringField(json, "build_role",
                                  progmem_to_string(BuildInfoConsts::str_build_role));
            appendJsonStringField(json, "generator_version",
                                  progmem_to_string(BuildInfoConsts::str_generator_version));
            appendJsonStringField(json, "project_name",
                                  progmem_to_string(BuildInfoConsts::str_project_name));
            appendJsonStringField(json, "pio_env",
                                  progmem_to_string(BuildInfoConsts::str_pio_env));
            appendJsonStringField(json, "board",
                                  progmem_to_string(BuildInfoConsts::str_board));
            appendJsonStringField(json, "built_at_utc",
                                  progmem_to_string(BuildInfoConsts::str_built_at_utc));
            appendJsonStringField(json, "code_tree_hash",
                                  progmem_to_string(BuildInfoConsts::str_code_tree_hash));
            json += "\"git\":{";
            appendJsonStringField(json, "repository_name",
                                  progmem_to_string(BuildInfoConsts::str_repository_name));
            appendJsonStringField(json, "branch",
                                  progmem_to_string(BuildInfoConsts::str_git_branch));
            appendJsonStringField(json, "commit_sha",
                                  progmem_to_string(BuildInfoConsts::str_git_commit_sha));
            appendJsonStringField(json, "commit_short",
                                  progmem_to_string(BuildInfoConsts::str_git_commit_short));
            appendJsonStringField(json, "exact_tag",
                                  progmem_to_string(BuildInfoConsts::str_git_exact_tag));
            appendJsonStringField(json, "nearest_tag",
                                  progmem_to_string(BuildInfoConsts::str_git_nearest_tag));
            json += "\"dirty\":";
            json += BuildInfoConsts::git_dirty ? "true" : "false";
            json += "}}";
            json += '}';

            auto payload = std::make_shared<std::string>(std::move(json));
            AsyncWebServerResponse *resp = beginOwnedResponse(
                request,
                reinterpret_cast<const char *>(FPSTR(BotServerWebConsts::mime_json)),
                payload);
            if (!resp)
            {
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "OOM");
                return;
            }
            resp->addHeader("Cache-Control",
                            FPSTR(BotServerWebConsts::cache_control_no_store));
            request->send(resp);
        });
}

// ---------------------------------------------------------------------------
// registerLogsRoute
// ---------------------------------------------------------------------------

void BotServerWeb::registerLogsRoute()
{
    server_->on(BotServerWebConsts::path_logs_api, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            std::vector<RollingLogger::LogEntry> rows;
            unsigned long version = 0;
            int max_rows = 0;
            int level = static_cast<int>(RollingLogger::INFO);

            if (logger_)
            {
                rows = logger_->get_log_rows();
                version = logger_->get_version();
                max_rows = logger_->get_max_rows();
                level = static_cast<int>(logger_->get_log_level());
            }

            std::string json;
            json.reserve(128 + rows.size() * 120);

            json += '{';
            json += "\"version\":";
            json += std::to_string(version);
            json += ",\"level\":";
            json += std::to_string(level);
            json += ",\"max_rows\":";
            json += std::to_string(max_rows);
            json += ",\"entries\":[";

            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i > 0)
                    json += ',';

                const RollingLogger::LogEntry &entry = rows[i];
                json += '{';
                json += "\"timestamp_ms\":";
                json += std::to_string(entry.timestamp_ms);
                json += ",\"level\":";
                json += std::to_string(static_cast<int>(entry.level));
                json += ",\"source\":\"";
                json += jsonEscape(entry.source);
                json += "\",\"message\":\"";
                json += jsonEscape(entry.message);
                json += "\"}";
            }

            json += "]}";

            auto payload = std::make_shared<std::string>(std::move(json));
            AsyncWebServerResponse *resp = beginOwnedResponse(
                request,
                reinterpret_cast<const char *>(FPSTR(BotServerWebConsts::mime_json)),
                payload);
            if (!resp)
            {
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "OOM");
                return;
            }
            resp->addHeader("Cache-Control",
                            FPSTR(BotServerWebConsts::cache_control_no_store));
            request->send(resp);
        });
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool BotServerWeb::start()
{
    server_ = new AsyncWebServer(port_);
    if (!server_)
    {
        if (logger_)
            logger_->error(FPSTR(BotServerWebConsts::msg_no_alloc));
        return false;
    }

    // Mount LittleFS — partition label must match platformio.ini board_build.partitions
    if (!LittleFS.begin(false, "/littlefs", 10,
                        reinterpret_cast<const char*>(FPSTR(BotServerWebConsts::littlefs_partition))))
    {
        if (logger_)
            logger_->error(FPSTR(BotServerWebConsts::msg_fs_failed));
        // Non-fatal: /botserver still works without the filesystem
    }
    else
    {
        if (logger_)
            logger_->info(FPSTR(BotServerWebConsts::msg_fs_ok));
    }

    register_get_botserver();
    registerBuildInfoRoute();
    registerLogsRoute();
    registerScriptRoutes();
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_botscript_html)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_botscript_html)),
                              reinterpret_cast<const char *>(FPSTR(BotServerWebConsts::mime_html)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_botscript_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_botscript_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_js)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_botscript_actions_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_botscript_actions_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_js)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_common_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_common_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_js)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_amaker_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_amaker_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_css)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_ws_joystick_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_ws_joystick_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_css)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_botscript_bundle_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_botscript_bundle_css)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_css)));
    registerLoggedStaticRoute(server_,
                              logger_,
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::path_botscript_bundle_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::fs_botscript_bundle_js)),
                              reinterpret_cast<const char *>(FPSTR(StaticRouteConsts::mime_js)));
    server_->on(BotServerWebConsts::path_echo, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);
            request->send(200, FPSTR(BotServerWebConsts::mime_text), "OK");
        });
    server_->on(BotServerWebConsts::path_fsprobe, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            File index_file = LittleFS.open("/www/index.html", "r");
            std::string body;
            body.reserve(128);

            body += "exists=";
            body += LittleFS.exists("/www/index.html") ? "1" : "0";
            body += "\nopened=";
            body += index_file ? "1" : "0";
            body += "\nsize=";
            body += index_file ? std::to_string(static_cast<unsigned long>(index_file.size())) : "0";
            body += "\n";

            if (index_file)
                index_file.close();

            request->send(200, FPSTR(BotServerWebConsts::mime_text), body.c_str());
        });
    server_->on("/", HTTP_GET, [this](AsyncWebServerRequest *request)   
    {
        logHttpRequest(logger_, request);
        request->redirect(FPSTR(BotServerWebConsts::root_redirect_target));
    });

    server_->onNotFound([this](AsyncWebServerRequest *request)
    {
        if (request->method() == HTTP_GET)
            logHttpRequest(logger_, request);
        ++dropped_count_;
        request->send(404, FPSTR(BotServerWebConsts::mime_html), "<html><body><h1>404 Not Found</h1> Try <a href=\"/\">/</a></body></html>");
    });
    // Static files served last so that /botserver takes precedence.
    // 1-day cache (86400 s) — use Ctrl+Shift+R in the browser to bypass during development.
    server_->serveStatic(reinterpret_cast<const char*>(FPSTR(BotServerWebConsts::static_url_root)),
                         LittleFS,
                         reinterpret_cast<const char*>(FPSTR(BotServerWebConsts::static_fs_root)))
           .setDefaultFile(reinterpret_cast<const char*>(FPSTR(BotServerWebConsts::default_file)))
           .setCacheControl(reinterpret_cast<const char*>(FPSTR(BotServerWebConsts::cache_control)));

    server_->begin();

    if (logger_)
        logger_->info(fpstr_to_string(FPSTR(BotServerWebConsts::msg_start_ok))
                      + std::to_string(port_),"WWW");
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void BotServerWeb::stop()
{
    if (!server_)
        return;

    server_->end();
    delete server_;
    server_ = nullptr;

    LittleFS.end();

    if (logger_)
        logger_->info(FPSTR(BotServerWebConsts::msg_stop));
}

// ---------------------------------------------------------------------------
// registerScriptRoutes
// ---------------------------------------------------------------------------

void BotServerWeb::registerScriptRoutes()
{
    // NOTE: /scripts/* handlers are registered BEFORE /scripts because
    // ESPAsyncWebServer matches a route registered as "/scripts" for any URL
    // that starts with "/scripts/" (e.g. "/scripts/foo.js"), so the more
    // specific wildcard pattern must come first to win the dispatch race.

    // ---- GET /scripts/<name>  →  script content as plain text ---------------
    server_->on(BotServerWebConsts::path_scripts_item, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            // URL is "/scripts/<name>" — strip the "/scripts/" prefix (9 chars)
            const std::string name(request->url().substring(9).c_str());
            if (!bot_.scriptExists(name))
            {
                request->send(404, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_script_not_found));
                return;
            }
            const std::string content = bot_.getScript(name);
            request->send(200, FPSTR(BotServerWebConsts::mime_text), content.c_str());
        });

    // ---- POST /scripts/<name>  →  save script (raw body = content) ----------
    server_->on(BotServerWebConsts::path_scripts_item, HTTP_POST,
        // Final handler: called once after the full body has been received
        [this](AsyncWebServerRequest *request)
        {
            // URL is "/scripts/<name>" — strip the "/scripts/" prefix (9 chars)
            const std::string name(request->url().substring(9).c_str());
            std::string *body = static_cast<std::string *>(request->_tempObject);
            const std::string content = body ? *body : std::string{};
            if (body) { delete body; request->_tempObject = nullptr; }

            if (!bot_.saveScript(name, content))
            {
                request->send(400, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_script_save_failed));
                return;
            }
            request->send(200, FPSTR(BotServerWebConsts::mime_text), "OK");
        },
        nullptr,  // upload handler (unused)
        // Body handler: accumulate raw body chunks into request->_tempObject
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
           size_t index, size_t /*total*/)
        {
            if (index == 0)
                request->_tempObject = new (std::nothrow) std::string();

            std::string *body = static_cast<std::string *>(request->_tempObject);
            if (body && (body->size() + len) <= BotServerWebConsts::max_script_body_size)
                body->append(reinterpret_cast<const char *>(data), len);
        });

    // ---- DELETE /scripts/<name>  →  delete script ---------------------------
    server_->on(BotServerWebConsts::path_scripts_item, HTTP_DELETE,
        [this](AsyncWebServerRequest *request)
        {
            // URL is "/scripts/<name>" — strip the "/scripts/" prefix (9 chars)
            const std::string name(request->url().substring(9).c_str());
            if (!bot_.deleteScript(name))
            {
                request->send(404, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_script_not_found));
                return;
            }
            request->send(200, FPSTR(BotServerWebConsts::mime_text), "OK");
        });

    // ---- GET /scripts  →  JSON array of script names -------------------------
    // Registered LAST so that the /scripts/* wildcard handlers above take
    // precedence for item URLs (see note at the top of this function).
    server_->on(BotServerWebConsts::path_scripts, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            const std::vector<std::string> names = bot_.listScripts();

            // Build compact JSON array without pulling in ArduinoJson
            std::string json;
            json.reserve(32 + names.size() * 24);
            json += '[';
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (i > 0) json += ',';
                json += '"';
                for (const char c : names[i])
                {
                    if (c == '"' || c == '\\') json += '\\';
                    json += c;
                }
                json += '"';
            }
            json += ']';

            request->send(200, FPSTR(BotServerWebConsts::mime_json), json.c_str());
        });

    if (logger_)
        logger_->info("Script routes registered (/scripts, /scripts/*)");
}

// ---------------------------------------------------------------------------
// registerCameraRoutes
// ---------------------------------------------------------------------------

void BotServerWeb::registerCameraRoutes(QueueHandle_t cam_queue)
{
    cam_queue_ = cam_queue;

    // ---- GET /cam/snapshot ----
    server_->on(BotServerWebConsts::path_snapshot, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            if (!cam_queue_)
            {
                request->send(503, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_cam_not_init));
                return;
            }
            if (streaming_active_)
            {
                request->send(503, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_cam_busy));
                return;
            }

            // Flush stale frames, keep only the latest
            camera_fb_t *fb = nullptr;
            camera_fb_t *latest = nullptr;
            while (xQueueReceive(cam_queue_, &fb, 0) == pdTRUE)
            {
                if (latest) esp_camera_fb_return(latest);
                latest = fb;
            }

            if (!latest)
            {
                request->send(503, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_cam_capture));
                return;
            }

            // Convert RGB565 → JPEG if needed
            uint8_t *jpg_buf = nullptr;
            size_t   jpg_len = 0;
            bool     owns_buf = false;

            const bool is_jpeg = latest->len >= 2 &&
                                 latest->buf[0] == 0xFF &&
                                 latest->buf[1] == 0xD8;
            if (is_jpeg)
            {
                jpg_buf  = latest->buf;
                jpg_len  = latest->len;
                owns_buf = false;
            }
            else
            {
                owns_buf = frame2jpg(latest, BotServerWebConsts::cam_jpeg_quality,
                                     &jpg_buf, &jpg_len);
            }

            esp_camera_fb_return(latest);

            if (!jpg_buf || jpg_len == 0)
            {
                if (owns_buf && jpg_buf) free(jpg_buf);
                request->send(503, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_cam_capture));
                return;
            }

            auto payload = std::make_shared<std::string>(
                reinterpret_cast<const char *>(jpg_buf),
                jpg_len);

            AsyncWebServerResponse *resp = beginOwnedResponse(
                request,
                reinterpret_cast<const char *>(FPSTR(BotServerWebConsts::mime_jpeg)),
                payload);
            if (!resp)
            {
                if (owns_buf) free(jpg_buf);
                request->send(500, FPSTR(BotServerWebConsts::mime_text), "OOM");
                return;
            }
            resp->addHeader("Content-Disposition",
                            FPSTR(BotServerWebConsts::snapshot_filename));
            resp->addHeader("Cache-Control", "no-store");
            request->send(resp);

            if (owns_buf) free(jpg_buf);
        });

    // ---- GET /cam/stream ----
    server_->on(BotServerWebConsts::path_stream, HTTP_GET,
        [this](AsyncWebServerRequest *request)
        {
            logHttpRequest(logger_, request);

            if (!cam_queue_)
            {
                request->send(503, FPSTR(BotServerWebConsts::mime_text),
                              FPSTR(BotServerWebConsts::err_cam_not_init));
                return;
            }

            streaming_active_ = true;

            // Per-frame state kept alive across chunked callback invocations.
            // shared_ptr ensures cleanup on connection close.
            struct StreamState
            {
                camera_fb_t *fb_held    = nullptr;  ///< held for JPEG zero-copy path
                uint8_t     *jpg_buf    = nullptr;  ///< data ptr (owned only in RGB565→JPEG path)
                size_t       jpg_len    = 0;
                size_t       offset     = 0;
                char         header[80] = {};
                size_t       header_len = 0;

                ~StreamState()
                {
                    if (fb_held) { esp_camera_fb_return(fb_held); fb_held = nullptr; }
                    else if (jpg_buf) { free(jpg_buf); jpg_buf = nullptr; }
                }
            };

            auto state = std::make_shared<StreamState>();

            AsyncWebServerResponse *response = request->beginChunkedResponse(
                FPSTR(BotServerWebConsts::mime_multipart),
                [this, state](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t
                {
                    if (!streaming_active_)
                        return 0; // end stream

                    // Acquire a fresh frame if we don't have one in progress
                    if (!state->jpg_buf)
                    {
                        camera_fb_t *fb = nullptr;
                        camera_fb_t *latest = nullptr;

                        // Drain stale frames — keep only the most recent
                        while (xQueueReceive(cam_queue_, &fb, 0) == pdTRUE)
                        {
                            if (latest) esp_camera_fb_return(latest);
                            latest = fb;
                        }

                        // If queue was empty, block briefly for the next frame.
                        // RESPONSE_TRY_AGAIN alone cannot reliably restart the
                        // chunked callback because AsyncTCP only re-triggers on
                        // TCP ACK — if no data was sent there is no ACK.
                        if (!latest)
                        {
                            xQueueReceive(cam_queue_, &latest,
                                          pdMS_TO_TICKS(BotServerWebConsts::cam_stream_wait_ms));
                        }

                        if (!latest || !latest->buf || latest->len == 0)
                        {
                            if (latest) esp_camera_fb_return(latest);
                            return RESPONSE_TRY_AGAIN;
                        }

                        const bool is_jpeg = latest->len >= 2 &&
                                             latest->buf[0] == 0xFF &&
                                             latest->buf[1] == 0xD8;
                        if (is_jpeg)
                        {
                            // Copy frame so the DMA buffer is returned to the
                            // camera task before the next capture cycle.
                            state->jpg_len = latest->len;
                            state->jpg_buf = static_cast<uint8_t *>(ps_malloc(state->jpg_len));
                            if (!state->jpg_buf)
                                state->jpg_buf = static_cast<uint8_t *>(malloc(state->jpg_len));
                            if (!state->jpg_buf)
                            {
                                esp_camera_fb_return(latest);
                                return RESPONSE_TRY_AGAIN;
                            }
                            memcpy(state->jpg_buf, latest->buf, state->jpg_len);
                            esp_camera_fb_return(latest); // release DMA buffer immediately
                        }
                        else
                        {
                            if (!frame2jpg(latest,
                                           BotServerWebConsts::cam_jpeg_quality,
                                           &state->jpg_buf, &state->jpg_len)
                                || !state->jpg_buf)
                            {
                                if (state->jpg_buf) { free(state->jpg_buf); state->jpg_buf = nullptr; }
                                esp_camera_fb_return(latest);
                                return RESPONSE_TRY_AGAIN;
                            }
                            esp_camera_fb_return(latest);
                        }

                        state->header_len = snprintf(
                            state->header, sizeof(state->header),
                            "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                            static_cast<unsigned>(state->jpg_len));
                        state->offset = 0;
                    }

                    // Send header + JPEG payload in chunks that fit maxLen
                    const size_t total     = state->header_len + state->jpg_len;
                    const size_t remaining = total - state->offset;
                    const size_t to_write  = (remaining < maxLen) ? remaining : maxLen;
                    size_t written = 0;
                    size_t pos     = state->offset;

                    while (written < to_write)
                    {
                        if (pos < state->header_len)
                        {
                            size_t chunk = std::min(to_write - written,
                                                    state->header_len - pos);
                            memcpy(buffer + written, state->header + pos, chunk);
                            written += chunk; pos += chunk;
                        }
                        else
                        {
                            size_t jpg_offset = pos - state->header_len;
                            size_t chunk = std::min(to_write - written,
                                                    state->jpg_len - jpg_offset);
                            memcpy(buffer + written,
                                   state->jpg_buf + jpg_offset, chunk);
                            written += chunk; pos += chunk;
                        }
                    }

                    state->offset += written;

                    if (state->offset >= total)
                    {
                        if (state->fb_held) { esp_camera_fb_return(state->fb_held); state->fb_held = nullptr; }
                        if (state->jpg_buf) { free(state->jpg_buf); }
                        state->jpg_buf    = nullptr;
                        state->jpg_len    = 0;
                        state->header_len = 0;
                        state->offset     = 0;
                    }

                    return written;
                });

            response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
            response->addHeader("Pragma",  "no-cache");
            response->addHeader("Expires", "0");
            response->addHeader("Access-Control-Allow-Origin", "*");

            request->onDisconnect([this]()
            {
                streaming_active_ = false;
                if (logger_) logger_->info("Camera stream client disconnected");
            });

            request->send(response);
            if (logger_) logger_->info("Camera MJPEG stream started");
        });

    if (logger_)
        logger_->info("Camera routes registered (/cam/snapshot, /cam/stream)");
}
