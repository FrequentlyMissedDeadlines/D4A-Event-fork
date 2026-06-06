/**
 * @file BotServerWeb.h
 * @brief HTTP server that exposes a single `/botserver` GET endpoint for
 *        binary bot protocol messages.
 *
 * @details
 * Because HTTP GET requests cannot carry a raw binary body, the binary frame
 * is passed as a **hex-encoded** query parameter `cmd`.  The server decodes it,
 * calls `BotMessageHandler::dispatch()`, and returns the binary response as
 * `application/octet-stream`.
 *
 * ## Request / response format
 *
 * ```
 * GET /botserver?cmd=<hex>  HTTP/1.1
 * ```
 *
 * | Scenario              | HTTP status | Body                              |
 * |-----------------------|-------------|-----------------------------------|
 * | Missing `cmd` param   | 400         | plain text error                  |
 * | Invalid hex string    | 400         | plain text error                  |
 * | dispatch() returns "" | 204         | empty (No Content)                |
 * | dispatch() returns data | 200       | raw binary, application/octet-stream |
 *
 * ### Example (curl)
 * ```bash
 * # Send action byte 0x41 followed by token "00000"
 * curl -o - "http://192.168.1.100/botserver?cmd=413030303030"
 * ```
 *
 * ## Lifecycle
 * @code
 *   BotServerWeb web_server(bot_handler);
 *   web_server.setBotMessageLogger(&debug_logger);
 *   web_server.start();       // call once in setup()
 *   // ...
 *   web_server.stop();
 * @endcode
 *
 * ## Thread safety
 * start() / stop() must be called from setup() (single-threaded).
 * The route handler runs in the AsyncTCP task and accesses only
 * `BotMessageHandler::dispatch()`, which is lock-free.
 */

#pragma once

#include <cstdint>
#include <string>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <pgmspace.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "services/AmakerBotService.h"

class RollingLogger;

// ---------------------------------------------------------------------------
// PROGMEM string constants
// ---------------------------------------------------------------------------
namespace BotServerWebConsts
{
    constexpr const char str_service_name[]   PROGMEM = "BotServerWeb";
    constexpr const char path_botserver[]     PROGMEM = "/botserver";
    constexpr const char path_echo[]          PROGMEM = "/echo";
    constexpr const char path_fsprobe[]       PROGMEM = "/fsprobe";
    constexpr const char path_buildinfo_api[] PROGMEM = "/api/buildinfo.json";
    constexpr const char param_cmd[]          PROGMEM = "cmd";
    constexpr const char mime_octet[]         PROGMEM = "application/octet-stream";
    constexpr const char mime_text[]          PROGMEM = "text/plain";
    constexpr const char mime_html[]          PROGMEM = "text/html";
    constexpr const char msg_start_ok[]       PROGMEM = "BotServerWeb listening on port ";
    constexpr const char msg_stop[]           PROGMEM = "BotServerWeb stopped";
    constexpr const char msg_no_alloc[]       PROGMEM = "BotServerWeb: allocation failed";
    constexpr const char err_missing_cmd[]    PROGMEM = "Missing 'cmd' query parameter";
    constexpr const char err_invalid_hex[]    PROGMEM = "Invalid hex in 'cmd' parameter";
    constexpr const char littlefs_partition[]  PROGMEM = "voice_data";
    constexpr const char static_fs_root[]        PROGMEM = "/www";
    constexpr const char static_url_root[]    PROGMEM = "/";
    constexpr const char default_file[]       PROGMEM = "index.html";
    constexpr const char cache_control[]      PROGMEM = "max-age=86400"; ///< 1 day
    constexpr const char cache_control_no_store[] PROGMEM = "no-store, max-age=0";
    constexpr const char msg_fs_ok[]          PROGMEM = "BotServerWeb: LittleFS mounted";
    constexpr const char msg_fs_failed[]      PROGMEM = "BotServerWeb: LittleFS mount failed (run uploadfs)";
    // ---- Camera routes ----
    constexpr const char path_snapshot[]      PROGMEM = "/cam/snapshot";
    constexpr const char path_stream[]        PROGMEM = "/cam/stream";
    constexpr const char mime_jpeg[]          PROGMEM = "image/jpeg";
    constexpr const char mime_multipart[]     PROGMEM = "multipart/x-mixed-replace; boundary=frame";
    constexpr const char snapshot_filename[]  PROGMEM = "inline; filename=snapshot.jpg";
    constexpr const char stream_boundary[]    PROGMEM = "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    constexpr const char err_cam_not_init[]   PROGMEM = "Camera not initialized";
    constexpr const char err_cam_busy[]       PROGMEM = "Snapshot unavailable during active stream";
    constexpr const char err_cam_capture[]    PROGMEM = "Camera capture failed";
    constexpr uint8_t    cam_jpeg_quality     = 10;   ///< JPEG quality for RGB565→JPEG fallback conversion
    constexpr uint32_t   cam_queue_timeout_ms = 10;   ///< Deprecated: snapshot path no longer blocks for new frame
    constexpr uint32_t   cam_stream_wait_ms   = 5;    ///< Short wait to reduce callback blocking under load
    constexpr uint16_t   default_port         = 80;
    // ---- Script filesystem routes ----
    constexpr const char path_scripts[]              PROGMEM = "/scripts";
    constexpr const char path_scripts_item[]         PROGMEM = "/scripts/*";
    constexpr const char mime_json[]                 PROGMEM = "application/json; charset=utf-8";
    constexpr const char err_script_not_found[]      PROGMEM = "Script not found";
    constexpr const char err_script_save_failed[]    PROGMEM = "Save failed";
    constexpr size_t     max_script_body_size        = 65536; ///< 64 KB cap per script body
} // namespace BotServerWebConsts

// ---------------------------------------------------------------------------
// BotServerWeb
// ---------------------------------------------------------------------------

/**
 * @brief Thin HTTP transport layer for the binary bot protocol.
 *
 * Owns one `AsyncWebServer` on port 80 (default).  Registers a single
 * `GET /botserver` route that hex-decodes the `cmd` parameter, dispatches
 * the resulting binary frame, and returns the binary response.
 */
class BotServerWeb
{
public:
    // ---- Construction ----

    /**
     * @brief Construct a BotServerWeb.
     * @param handler Central message dispatcher — must outlive this object.
     * @param port    HTTP listen port (default 80).
     */
    explicit BotServerWeb(AmakerBotService &bot,
                          uint16_t port = BotServerWebConsts::default_port);

    // Not copyable — owns a heap-allocated server
    BotServerWeb(const BotServerWeb &)            = delete;
    BotServerWeb &operator=(const BotServerWeb &) = delete;

    ~BotServerWeb() { stop(); }

    // ---- Configuration (call before start()) ----

    /**
     * @brief Attach a debugLogger for info / error output.
     * @param log May be nullptr to disable logging.
     */
    void setBotMessageLogger(RollingLogger *log) { logger_ = log; }

    /**
     * @brief Override the listen port.
     * @note Has no effect after start() has been called.
     */
    void setPort(uint16_t port) { port_ = port; }

    /** @brief Return the configured listen port. */
    uint16_t getPort() const { return port_; }

    // ---- Lifecycle ----

    /**
     * @brief Allocate the AsyncWebServer, register the `/botserver` route,
     *        and start listening.
     * @return true  on success
     * @return false if allocation failed
     */
    bool start();

    /**
     * @brief Stop the web server and free resources.
     * Safe to call even if start() was never called.
     */
    void stop();

    /** @brief Return true if the server is currently running. */
    bool isRunning() const { return server_ != nullptr; }

    /**
     * @brief Register /cam/snapshot and /cam/stream routes.
     *
     * Must be called after start().  Registers two routes:
     *   - GET /cam/snapshot  — returns a single JPEG image
     *   - GET /cam/stream    — MJPEG multipart/x-mixed-replace stream
     *
     * @param cam_queue  FreeRTOS queue populated by the UNIHIKER K10 camera task
     *                   (xQueueCamera from unihiker_k10).  Must remain valid for
     *                   the lifetime of this server.
     */
    void registerCameraRoutes(QueueHandle_t cam_queue);

    // ---- Diagnostics ----

    /** @brief Number of valid requests dispatched since start(). */
    uint32_t getRxCount()      const { return rx_count_; }
    /** @brief Number of responses sent since start(). */
    uint32_t getTxCount()      const { return tx_count_; }
    /** @brief Number of requests rejected (bad / missing param). */
    uint32_t getDroppedCount() const { return dropped_count_; }

private:
    AmakerBotService &bot_;
    RollingLogger     *logger_          = nullptr;
    uint16_t           port_;
    AsyncWebServer    *server_          = nullptr;
    QueueHandle_t      cam_queue_       = nullptr;
    volatile bool      streaming_active_ = false;

    volatile uint32_t  rx_count_        = 0;
    volatile uint32_t  tx_count_        = 0;
    volatile uint32_t  dropped_count_   = 0;

    /**
     * @brief Register the GET /botserver route on server_.
     */
    void register_get_botserver();

    /**
     * @brief Register GET /scripts, GET/POST/DELETE /scripts/<name> routes.
     * Must be called from start() before server_->begin().
     */
    void registerScriptRoutes();

    /**
     * @brief Register GET /api/buildinfo.json route.
     *
     * The returned payload describes the running firmware only.
     * Filesystem metadata is generated separately during `buildfs` / `uploadfs`
     * and served as the static file `/buildinfo.json`.
     */
    void registerBuildInfoRoute();

    /**
     * @brief Decode a hex string into a binary std::string.
     * @param hex     Input hex string (must have even length, 0-9 a-f A-F)
     * @param hex_len Length of the hex string
     * @param out     Decoded binary output
     * @return true  on success
     * @return false if hex is empty, odd-length, or contains invalid chars
     */
    static bool hexDecode(const char *hex, size_t hex_len, std::string &out);

    /**
     * @brief Encode a binary buffer as a lowercase hex string.
     * @param data    Input binary buffer
     * @param len     Number of bytes to encode
     * @return std::string Hex-encoded string (always 2×len characters)
     */
    static std::string hexEncode(const uint8_t *data, size_t len);
};
