#include <stdio.h>
// Include AsyncWebServer BEFORE Arduino.h to avoid HTTP method enum conflicts
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/task.h>
#include <TFT_eSPI.h>
#include <esp_log.h>
#include <AsyncUDP.h>
#include <unihiker_k10.h>
#include <esp_camera.h>
#include "RollingLogger.h"
#include "IsServiceInterface.h"
#include <esp_log.h>
#include <stdio.h>
#include "BotCommunication/BotServerUDP.h"
#include "BotCommunication/BotServerWeb.h"
#include "BotCommunication/BotServerWebSocket.h"
#include "BotCommunication/BotServerBLE.h"
#include "services/WiFiService.h"
#include "services/AmakerBotService.h"
#include "services/AmakerBotUIService.h"
#include "services/MotorServoService.h"
#include "services/LEDService.h"
#include "ESPLogToRolling.h"
#define LOAD_FONT8    // TFT font - special case for library config

// Main application constants
namespace MainConsts
{
  constexpr const char msg_http_task_started[] PROGMEM = "HTTP server task started";
  constexpr const char msg_webserver_running[] PROGMEM = "WebServer task running...";
  constexpr const char msg_service[] PROGMEM = "Service ";
  constexpr const char msg_openapi_registered[] PROGMEM = "OpenAPI registered ";
  constexpr const char msg_register_failed[] PROGMEM = "registerOpenAPIService failed for ";
  constexpr const char msg_no_openapi[] PROGMEM = "No OpenAPI for ";
  constexpr const char msg_udp_handler_registered[] PROGMEM = "UDP handler registered: ";
  constexpr const char msg_starting_services[] PROGMEM = "Starting services...";
  constexpr const char msg_fatal_wifi_failed[] PROGMEM = "FATAL : WiFi failed to start.";
  constexpr const char msg_failed_udp[] PROGMEM = "Failed to start UDP service";
  constexpr const char msg_failed_webserver[] PROGMEM = "Failed to start webserver";
  constexpr const char msg_bot_started[] PROGMEM = "Bot ";
  constexpr const char msg_udp_port[] PROGMEM = "UDP port:";
  constexpr const char str_serial_init_display[] PROGMEM = "[INIT] Display initialized";
}

namespace
{
  constexpr uint16_t web_port = 80;
  constexpr TickType_t udp_task_delay_ticks = pdMS_TO_TICKS(10);
  constexpr TickType_t display_task_delay_ticks = pdMS_TO_TICKS(500);
  constexpr TickType_t display_update_interval_ticks = pdMS_TO_TICKS(500);
  constexpr TickType_t web_server_task_delay_ticks = pdMS_TO_TICKS(10);
  constexpr uint8_t wifi_max_attempts = 20;
  constexpr uint16_t wifi_attempt_delay_ms = 500;
  constexpr uint32_t serial_baud = 115200;
  constexpr uint32_t color_ok = 0x00D000;
  constexpr uint32_t color_error = 0xD00000;
  constexpr uint32_t color_info = 0xD0D0D0;
  constexpr uint32_t color_subtle = 0xE0E0E0;

  // ---------- FreeRTOS task configuration ----------
  /// Core 0: UDP + WebSocket transport (real-time, max priority)
  constexpr BaseType_t transport_core = 0;
  constexpr UBaseType_t transport_priority = configMAX_PRIORITIES - 1;
  constexpr uint32_t transport_stack = 8192;

  /// Core 1: Web server + other services (best-effort)
  constexpr BaseType_t web_core = 1;
  constexpr UBaseType_t web_priority = 5;
  constexpr uint32_t web_stack = 8192;


  // Separate buffer for display (non-blocking)

}

// Core 0 should serve websocket and UDP
// Core 1 should serve display and webserver

TFT_eSPI tft;
UNIHIKER_K10 unihiker;
RollingLogger debug_logger = RollingLogger();
RollingLogger svc_logger   = RollingLogger();
RollingLogger bot_logger   = RollingLogger();
RollingLogger esp_logger   = RollingLogger();
WifiService wifi_service_ = WifiService();
MotorServoService motor_servo_ = MotorServoService();
extern DFR1216_I2C board;  ///< Defined in DFR1216.cpp; registered as 0x03 service handler
LEDService led_service_(unihiker, board);
AmakerBotService amaker_bot_ = AmakerBotService({&motor_servo_, &board, &led_service_});
BotServerUDP bot_over_udp_ = BotServerUDP(amaker_bot_);
BotServerWeb bot_over_web_ = BotServerWeb(amaker_bot_);
BotServerWebSocket bot_over_websocket_ = BotServerWebSocket(amaker_bot_);
// BotServerBLE bot_over_ble_ = BotServerBLE(amaker_bot_);
AmakerBotUIService ui_service =
    AmakerBotUIService(unihiker,
                       wifi_service_,
                       amaker_bot_,
                       bot_over_web_,
                       bot_over_udp_,
                       bot_over_websocket_,
                       motor_servo_);

namespace
{

  /**
   * @fn start_service
   * @brief Initialize and start a service, attaching the debug logger. Uses LED 0 to express state.
   * @param service Reference to the ServiceInterface to start.
   * @return true if the service started successfully, false otherwise.
   */
  bool start_service(IsServiceInterface &service)
  {
    // If the service supports logging, attach the global debug logger.
    unihiker.rgb->write(0, 32, 0, 0); // pixel0 = red

#ifdef VERBOSE_DEBUG
    debug_logger.debug(progmem_to_string(MainConsts::msg_service) + service.getServiceName());
#endif
    service.setDebugLogger(&debug_logger);
    service.setServiceLogger(&svc_logger);

    // Attach settings service if this is not the settings service itself
    // if (&service != static_cast<IsServiceInterface *>(&settings_service))
    // {
    //   service.setSettingsService(&settings_service);
    // }

    if (service.initializeService())
    {
      unihiker.rgb->write(0, 32, 32, 0); // pixel0 = yellow (initialised, starting)
      if (!service.startService())
      {
        bot_logger.error(service.getServiceName() + " " + FPSTR(ServiceConst::msg_start_failed));
        unihiker.rgb->write(0, 0, 0, 0);
        return false;
      }
      else
      {
        unihiker.rgb->write(0, 0, 32, 0); // pixel0 = green (started ok)
#ifdef VERBOSE_DEBUG
        bot_logger.debug(service.getServiceName() + " " + FPSTR(ServiceConst::msg_start_ok));
#endif
      }
    }
    else
    {
      bot_logger.error(service.getServiceName() + " " + FPSTR(ServiceConst::msg_init_failed));
      unihiker.rgb->write(0, 0, 0, 0);
      return false;
    }

    // Register OpenAPI routes if the service implements IsOpenAPIInterface

    unihiker.rgb->write(0, 0, 0, 0);

    return true;
  }

}

// ---------------------------------------------------------------------------
// FreeRTOS tasks
// ---------------------------------------------------------------------------

/**
 * @brief Core 0 — UDP + WebSocket transport at maximum priority.
 *
 * Only one transport is active at a time (UDP vs WebSocket), so sharing a
 * single high-priority core keeps latency minimal and avoids contention.
 * Both servers are event-driven; this task blocks permanently after init.
 */
void xtask_bot_transport(void * /*pvParameters*/)
{
  bot_over_udp_.setBotMessageLogger(&debug_logger);
  bot_over_udp_.setPort(24642);
  bot_over_udp_.start();

  bot_over_websocket_.setBotMessageLogger(&debug_logger);
  bot_over_websocket_.setPort(81);
  bot_over_websocket_.start();

  debug_logger.info("BotTransport task ready (UDP:24642 WS:81) on core 0","main");

  // Both servers are fully event-driven — no polling needed.
  for (;;) {
    vTaskDelay(portMAX_DELAY);}
}

/**
 * @brief Core 1 — HTTP web server and future service initialisation.
 *
 * Runs at normal priority so it never pre-empts the transport task on Core 0.
 * Additional services (OpenAPI, settings, …) should be started here.
 */
void xtask_web_server(void * /*pvParameters*/)
{
  bot_over_web_.setBotMessageLogger(&debug_logger);
  bot_over_web_.setPort(80);
  bot_over_web_.start();

  // Initialize camera and register /cam/snapshot + /cam/stream routes
  static QueueHandle_t cam_queue = xQueueCreate(2, sizeof(camera_fb_t *));
  if (cam_queue) {
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_HVGA, 2, cam_queue);
    esp_log_level_set("cam_hal", ESP_LOG_ERROR); // suppress noisy queue-full warnings
    bot_over_web_.registerCameraRoutes(cam_queue);
    debug_logger.info("Camera routes registered (/cam/snapshot, /cam/stream)");
  } else {
    debug_logger.error("Camera queue allocation failed — camera routes not registered","main");
  }

  debug_logger.info("WebServer task ready (HTTP:80) on core 1","main");

  for (;;) {
    ui_service.tick();
    vTaskDelay(display_update_interval_ticks);
  }
}

/**
 * @brief Arduino setup function - initializes hardware and services
 * @details Initializes display, loggers, services, and FreeRTOS tasks
 */
void setup()
{

  // Initialize Serial FIRST for debugging
  #ifdef SERIAL_DEBUG
  Serial.begin(serial_baud);
  delay(100);
  Serial.println("\n\n=== K10-Bot Starting ===");
  delay(500);
  #endif
  // Small delay to ensure system stabilizes

  // Configure ESP-IDF logging AS FIRST THING before any hardware init
  esp_log_level_set("*", ESP_LOG_DEBUG);

  // Initialize loggers BEFORE hardware to capture early logs
  bot_logger.set_max_rows(40);
  bot_logger.set_log_level(RollingLogger::INFO);
  svc_logger.set_max_rows(40);
  svc_logger.set_log_level(RollingLogger::INFO);
  debug_logger.set_max_rows(40);
  debug_logger.set_log_level(RollingLogger::DEBUG);
  esp_logger.set_max_rows(40);
  esp_logger.set_log_level(RollingLogger::DEBUG);
#ifdef SERIAL_DEBUG
  Serial.println("Loggers initialized");
#endif
  // Redirect ESP-IDF logs BEFORE any other initialization
  esp_log_to_rolling_init(&esp_logger);
  #ifdef SERIAL_DEBUG
  Serial.println("ESP-IDF logging redirected");
#endif
  // Now initialize hardware
    #ifdef SERIAL_DEBUG
  Serial.println("Initializing UniHiker hardware...");
  #endif
  unihiker.begin();
    #ifdef SERIAL_DEBUG
  Serial.println("Initializing display...");
  #endif
  unihiker.initScreen(2, 30);
  unihiker.creatCanvas();
  unihiker.setScreenBackground(TFT_BLACK);

  unihiker.rgb->write(0, 0, 0, 0);
  unihiker.rgb->write(1, 0, 0, 0);
  unihiker.rgb->write(2, 0, 0, 0);

  ui_service.setDebugLogger(&debug_logger);
  ui_service.setServiceLogger(&svc_logger);
  ui_service.setShownLoggers(&bot_logger, &svc_logger, &debug_logger, &esp_logger);
  ui_service.initializeService();
  ui_service.startService();

  // Register UI service as a bot message handler for remote screen control
  amaker_bot_.registerHandler(&ui_service);

  // Initialize DFR1216 board as a full service before MotorServoService
  // (MotorServoService::initializeService() requires board.getStatus() == STARTED)
  start_service(board);
  if (board.isServiceStarted())
  {
    start_service(motor_servo_);
    start_service(led_service_);
  }
  else
  {
    bot_logger.warning("Skipping MotorServo and LED services because DFR1216 failed to start", "main");
  }
  start_service(amaker_bot_);

  // Inject WifiService so AmakerBotService can handle CMD_GET/SET/RESET_WIFI
  amaker_bot_.setWifiService(&wifi_service_);

  start_service(wifi_service_);

  // BLE must be initialised here in setup() — NOT inside a max-priority FreeRTOS
  // task. NimBLEDevice::init() blocks until the NimBLE host task starts; if called
  // from a max-priority task that task starves NimBLE's lower-priority host task,
  // causing a watchdog reboot.
  // bot_over_ble_.setBotMessageLogger(&debug_logger);
  // bot_over_ble_.setDeviceName(wifi_service_.getHostname());
  // bot_over_ble_.start();

  // Core 0 — UDP + WebSocket at maximum priority (real-time transport)
  xTaskCreatePinnedToCore(
      xtask_bot_transport,
      "BotTransport",
      transport_stack,
      nullptr,
      transport_priority,
      nullptr,
      transport_core);

  // Core 1 — HTTP web server + display (best-effort)
  xTaskCreatePinnedToCore(
      xtask_web_server,
      "WebServer",
      web_stack,
      nullptr,
      web_priority,
      nullptr,
      web_core);

}

/**
 * @brief Arduino main loop function.
 */
void loop()
{
  // All application logic runs inside FreeRTOS tasks
  // we can remove the task with 
  vTaskDelete(nullptr); 
  // or we could make a  loooooong sleep on it.
    //vTaskDelay(portMAX_DELAY); 
}
