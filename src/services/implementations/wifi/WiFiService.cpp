/**
 * @file WiFiService.cpp
 * @brief WiFi connectivity service — STA mode with automatic AP fallback.
 *
 * @details Credentials default to compile-time values (development only).
 *          At runtime they are loaded from / saved to NVS via the ESP32
 *          Preferences library (namespace "wifi").
 *
 * @warning SECURITY — the default STA credentials below are for development
 *          and testing only.  Do NOT commit real credentials to source control.
 *          Use loadSettings() / saveSettings() to manage credentials securely
 *          at provisioning time.
 */

#include <WiFi.h>
#include <Preferences.h>
#include "services/WiFiService.h"
#include "RollingLogger.h"
#include "FlashStringHelper.h"
#include <Arduino.h>    // millis(), delay(), ESP

#ifndef WIFI_AP_CHANNEL
#define WIFI_AP_CHANNEL 1
#endif

static_assert(WIFI_AP_CHANNEL >= 1 && WIFI_AP_CHANNEL <= 13,
              "WIFI_AP_CHANNEL must be in range [1, 13]");

// ---------------------------------------------------------------------------
// PROGMEM string constants + compile-time defaults
// ---------------------------------------------------------------------------
namespace WiFiConsts
{
    // ---- Compile-time default credentials (development only) ----
    /// @warning Do NOT commit real credentials to source control :)
    constexpr const char default_wifi_ssid[]     = "No default SSID";  // <-- REPLACE BEFORE COMMITTING
    constexpr const char default_wifi_password[] = "No default password";  // <-- REPLACE BEFORE COMMITTING
    constexpr const char default_ap_ssid[]       = "aMaker-";
    constexpr const char default_ap_password[]   = "amaker-club";
    constexpr const char default_hostname[]      = "amakerbot-";
    constexpr uint8_t default_ap_channel         = WIFI_AP_CHANNEL;

    constexpr uint8_t  wifi_conn_max_attempts = 8;    ///< Attempts before giving up on STA
    constexpr uint32_t wifi_conn_sleep_ms     = 500;  ///< ms between connection attempts

    constexpr const char str_service_name[]     PROGMEM = "WiFi Service";
    constexpr const char path_service[]         PROGMEM = "wifi/v1";

    constexpr const char msg_starting[]         PROGMEM = "Starting WiFi service...";
    constexpr const char msg_activation[]       PROGMEM = "Activating WiFi.";
    constexpr const char msg_connecting_to[]    PROGMEM = "Connecting to ";
    constexpr const char msg_attempt[]          PROGMEM = " attempt #";
    constexpr const char msg_connected[]        PROGMEM = "WiFi STA: ";
    constexpr const char msg_failed_connect[]   PROGMEM = "Failed to connect: ";
    constexpr const char msg_fallback_ap[]      PROGMEM = "Falling back to AP mode.";
    constexpr const char msg_ap_created[]       PROGMEM = "WiFi AP: ";
    constexpr const char msg_ap_failed[]        PROGMEM = "Failed to create AP: ";
    constexpr const char msg_missing_ssid[]     PROGMEM = "Missing SSID.";
    constexpr const char msg_missing_password[] PROGMEM = "Missing password.";
    constexpr const char msg_start_ok[]         PROGMEM = "WiFi started successfully.";
    constexpr const char msg_settings_loaded[]  PROGMEM = "WiFi settings loaded.";
    constexpr const char msg_settings_saved[]   PROGMEM = "WiFi settings saved.";
    constexpr const char msg_settings_reset[]   PROGMEM = "WiFi settings reset to defaults.";
    constexpr const char msg_prefs_open_fail[]  PROGMEM = "WiFi: Preferences open failed.";

    // NVS keys — plain C strings (Preferences uses them directly)
    constexpr const char nvs_namespace[]   = "wifi";
    constexpr const char nvs_ssid[]        = "ssid";
    constexpr const char nvs_password[]    = "password";
    constexpr const char nvs_ap_ssid[]     = "ap_ssid";
    constexpr const char nvs_ap_password[] = "ap_password";
    constexpr const char nvs_hostname[]    = "hostname";
}

// ---------------------------------------------------------------------------
// Module-level state — credentials are WifiService member variables
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
WifiService::WifiService()
{
    // Initialise member variables with compile-time defaults
    wifi_ssid_     = WiFiConsts::default_wifi_ssid;
    wifi_password_ = WiFiConsts::default_wifi_password;
    ap_ssid_       = WiFiConsts::default_ap_ssid;
    ap_password_   = WiFiConsts::default_ap_password;
    hostname_      = WiFiConsts::default_hostname;

    // Build a 6-char uppercase hex suffix from the upper 3 bytes of the ESP32 MAC
    char mac_buf[8];
    snprintf(mac_buf, sizeof(mac_buf), "%06X",
             static_cast<uint32_t>(ESP.getEfuseMac() >> 24));
    mac_suffix_ = std::string(mac_buf);
}
std::string WifiService::getIP()
{
    return connected_ip_;
}

std::string WifiService::getSSID()
{
    return connected_ssid_;
}

std::string WifiService::getHostname()
{
    return hostname_ + mac_suffix_;
}

std::string WifiService::getServiceName()
{
    return progmem_to_string(WiFiConsts::str_service_name);
}

// ---------------------------------------------------------------------------
// open_access_point
// ---------------------------------------------------------------------------

/**
 * @brief Create a WiFi SoftAP using the configured AP credentials.
 *        The full SSID is AP_SSID + MAC suffix (e.g. "aMaker-A3F2B1").
 */
bool WifiService::open_access_point()
{
    const std::string full_ap_ssid = ap_ssid_ + mac_suffix_;

    if (debugLogger)
    {
        debugLogger->info(std::string("AP SSID: ") + full_ap_ssid);
        debugLogger->info(std::string("Hostname: ") + getHostname());
        debugLogger->info(std::string("AP channel: ") + std::to_string(WiFiConsts::default_ap_channel));
    }

    WiFi.disconnect(true);
    delay(100);

    // AP+STA keeps the radio available for both modes
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(getHostname().c_str());

    if (!WiFi.softAP(full_ap_ssid.c_str(),
                     ap_password_.c_str(),
                     WiFiConsts::default_ap_channel))
    {
        if (debugLogger)
            debugLogger->error(FPSTR(WiFiConsts::msg_ap_failed) + full_ap_ssid);
        return false;
    }

    connected_ip_   = WiFi.softAPIP().toString().c_str();
    connected_ssid_ = full_ap_ssid;

    if (debugLogger)
        debugLogger->warning(FPSTR(WiFiConsts::msg_ap_created) + full_ap_ssid
                        + " " + connected_ip_);
    return true;
}

// ---------------------------------------------------------------------------
// connect_to_wifi
// ---------------------------------------------------------------------------

/**
 * @brief Attempt to connect to an existing WiFi network.
 *        Retries WiFiConsts::wifi_conn_max_attempts times before returning false.
 */
bool WifiService::connect_to_wifi(const std::string &ssid,
                                  const std::string &password)
{

    if (ssid.empty())
    {
        if (serviceLogger)
            serviceLogger->warning(FPSTR(WiFiConsts::msg_missing_ssid));
        return false;
    }
    if (password.empty())
    {
        if (serviceLogger)
            serviceLogger->warning(FPSTR(WiFiConsts::msg_missing_password));
        return false;
    }

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    if (debugLogger)
        debugLogger->info(FPSTR(WiFiConsts::msg_connecting_to) + ssid);

    for (int i = 0; i < WiFiConsts::wifi_conn_max_attempts && WiFi.status() != WL_CONNECTED; ++i)
    {
        if (debugLogger)
            debugLogger->info(FPSTR(WiFiConsts::msg_attempt)
                         + std::to_string(i + 1)
                         + std::string("/")
                         + std::to_string(WiFiConsts::wifi_conn_max_attempts));
        delay(WiFiConsts::wifi_conn_sleep_ms);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        connected_ip_   = WiFi.localIP().toString().c_str();
        connected_ssid_ = ssid;
        if (debugLogger)
            debugLogger->info(FPSTR(WiFiConsts::msg_connected) + ssid + " " + connected_ip_);
        return true;
    }

    if (debugLogger)
        debugLogger->error(FPSTR(WiFiConsts::msg_failed_connect) + ssid);
    return false;
}

// ---------------------------------------------------------------------------
// disconnect_from_wifi
// ---------------------------------------------------------------------------

bool WifiService::disconnect_from_wifi()
{
    WiFi.disconnect(true);
    delay(100);
    connected_ip_.clear();
    connected_ssid_.clear();
    return true;
}

// ---------------------------------------------------------------------------
// connect_and_fallback
// ---------------------------------------------------------------------------

/**
 * @brief Try STA connection; open AP on failure.
 */
bool WifiService::connect_and_fallback(const std::string &ssid,
                                       const std::string &password)
{
    if (debugLogger)
        debugLogger->info(FPSTR(WiFiConsts::msg_activation));

    if (connect_to_wifi(ssid, password))
        return true;
    if(serviceLogger) serviceLogger->warning(FPSTR(WiFiConsts::msg_fallback_ap));

    return open_access_point();
}

// ---------------------------------------------------------------------------
// wifi_activation  (public convenience wrapper)
// ---------------------------------------------------------------------------

bool WifiService::wifi_activation()
{
    return connect_and_fallback(wifi_ssid_, wifi_password_);
}

// ---------------------------------------------------------------------------
// initializeService
// ---------------------------------------------------------------------------

bool WifiService::initializeService()
{
    // Load credentials first so startService() uses up-to-date values
    loadSettings();




    setServiceStatus(INITIALIZED);
#ifdef VERBOSE_DEBUG
    if (debugLogger)
        debugLogger->debug(getServiceName() + " " + getStatusString());
#endif
    return true;
}

// ---------------------------------------------------------------------------
// startService
// ---------------------------------------------------------------------------

bool WifiService::startService()
{
#ifdef VERBOSE_DEBUG
    if (debugLogger)
        debugLogger->info(FPSTR(WiFiConsts::msg_starting));
#endif

    const bool ok = connect_and_fallback(wifi_ssid_, wifi_password_);

    if (ok)
      {  setServiceStatus( STARTED);}
    else {
         if (serviceLogger) serviceLogger->warning(FPSTR(ServiceConst::msg_start_failed)); 
         START_FAILED;
    }
     

#ifdef VERBOSE_DEBUG
    if (debugLogger)
        debugLogger->debug(getServiceName() + " " + getStatusString());
#endif
    return ok;
}

// ---------------------------------------------------------------------------
// stopService
// ---------------------------------------------------------------------------

bool WifiService::stopService()
{
    const bool ok = disconnect_from_wifi();
    setServiceStatus(ok ? STOPPED : STOP_FAILED);
#ifdef VERBOSE_DEBUG
    if (debugLogger)
        debugLogger->debug(getServiceName() + " " + getStatusString());
#endif
    return ok;
}

// ---------------------------------------------------------------------------
// saveSettings  — NVS via Preferences
// ---------------------------------------------------------------------------

bool WifiService::saveSettings()
{
    Preferences prefs;
    if (!prefs.begin(WiFiConsts::nvs_namespace, /* readOnly= */ false))
    {
        if (debugLogger)
            debugLogger->error(FPSTR(WiFiConsts::msg_prefs_open_fail));
        return false;
    }

    prefs.putString(WiFiConsts::nvs_ssid,        wifi_ssid_.c_str());
    prefs.putString(WiFiConsts::nvs_password,     wifi_password_.c_str());
    prefs.putString(WiFiConsts::nvs_ap_ssid,      ap_ssid_.c_str());
    prefs.putString(WiFiConsts::nvs_ap_password,  ap_password_.c_str());
    prefs.putString(WiFiConsts::nvs_hostname,     hostname_.c_str());
    prefs.end();

    if (debugLogger)
        debugLogger->info(FPSTR(WiFiConsts::msg_settings_saved));
    return true;
}

// ---------------------------------------------------------------------------
// loadSettings  — NVS via Preferences
// ---------------------------------------------------------------------------

bool WifiService::loadSettings()
{
    Preferences prefs;
    if (!prefs.begin(WiFiConsts::nvs_namespace, /* readOnly= */ true))
    {
        // No saved settings yet — silently keep compile-time defaults
        return false;
    }

    wifi_ssid_     = prefs.getString(WiFiConsts::nvs_ssid,        WiFiConsts::default_wifi_ssid).c_str();
    wifi_password_ = prefs.getString(WiFiConsts::nvs_password,     WiFiConsts::default_wifi_password).c_str();
    ap_ssid_       = prefs.getString(WiFiConsts::nvs_ap_ssid,      WiFiConsts::default_ap_ssid).c_str();
    ap_password_   = prefs.getString(WiFiConsts::nvs_ap_password,  WiFiConsts::default_ap_password).c_str();
    hostname_      = prefs.getString(WiFiConsts::nvs_hostname,     WiFiConsts::default_hostname).c_str();
    prefs.end();

    if (debugLogger)
    {
        debugLogger->info(FPSTR(WiFiConsts::msg_settings_loaded));
        debugLogger->info(std::string("- STA SSID: ")  + wifi_ssid_);
        debugLogger->info(std::string("- AP  SSID: ")  + ap_ssid_ + mac_suffix_);
        debugLogger->info(std::string("- Hostname: ")  + getHostname());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Settings accessors & mutators
// ---------------------------------------------------------------------------

/**
 * @brief Return the configured STA SSID.
 */
std::string WifiService::getWifiSsid() const
{
    return wifi_ssid_;
}

/**
 * @brief Return the configured STA password.
 */
std::string WifiService::getWifiPassword() const
{
    return wifi_password_;
}

/**
 * @brief Update the STA credentials in memory (does NOT write to NVS).
 *        Call saveSettings() to persist, or applySettings() to reconnect.
 */
void WifiService::setWifiCredentials(const std::string &ssid, const std::string &password)
{
    wifi_ssid_     = ssid;
    wifi_password_ = password;
}

// ---------------------------------------------------------------------------
// resetSettings  — wipe NVS namespace and restore compile-time defaults
// ---------------------------------------------------------------------------

/**
 * @brief Clear the NVS "wifi" namespace and restore compile-time defaults.
 * @return true always (NVS absence is treated as a no-op, not an error).
 */
bool WifiService::resetSettings()
{
    Preferences prefs;
    if (prefs.begin(WiFiConsts::nvs_namespace, /* readOnly= */ false))
    {
        prefs.clear();
        prefs.end();
    }

    wifi_ssid_     = WiFiConsts::default_wifi_ssid;
    wifi_password_ = WiFiConsts::default_wifi_password;
    ap_ssid_       = WiFiConsts::default_ap_ssid;
    ap_password_   = WiFiConsts::default_ap_password;
    hostname_      = WiFiConsts::default_hostname;

    if (debugLogger)
        debugLogger->info(FPSTR(WiFiConsts::msg_settings_reset));
    return true;
}

// ---------------------------------------------------------------------------
// applySettings  — reconnect with current in-memory credentials
// ---------------------------------------------------------------------------

/**
 * @brief Disconnect and reconnect using the current in-memory credentials.
 *        Intended to be called after setWifiCredentials() + saveSettings().
 * @return true if either STA or AP mode is active.
 */
bool WifiService::applySettings()
{
    return connect_and_fallback(wifi_ssid_, wifi_password_);
}
