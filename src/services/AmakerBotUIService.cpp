/**
 * @file AmakerBotUIService.cpp
 * @brief K10 TFT screen manager — delegates rendering to AppScreen and LogScreen.
 *
 * @note All TFT calls use the global `tft` (TFT_eSPI) declared in main.cpp.
 *       The display task (Core 1) must call tick() at ~500 ms intervals.
 */

#include "services/AmakerBotUIService.h"
#include "FlashStringHelper.h"
#include "BotCommunication/BotServerUDP.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

AmakerBotUIService::AmakerBotUIService(UNIHIKER_K10        &k10,
                                        WifiService         &wifi,
                                        AmakerBotService    &amakerbot,
                                        BotServerWeb        &web,
                                        BotServerUDP        &udp,
                                        BotServerWebSocket  &ws,
                                        MotorServoService   &motor_servo)
    : k10_(k10),
      app_screen_(wifi, amakerbot, web, udp, ws, motor_servo)
{
}

AmakerBotUIService::~AmakerBotUIService()
{
    for (auto *&s : log_screens_)
    {
        delete s;
        s = nullptr;
    }
}

// ---------------------------------------------------------------------------
// setShownLoggers — create one LogScreen per non-null logger
// ---------------------------------------------------------------------------

void AmakerBotUIService::setShownLoggers(RollingLogger *app,
                                     RollingLogger *svc,
                                     RollingLogger *debug,
                                     RollingLogger *esp)
{
    // Release any previously created screens
    for (auto *&s : log_screens_)
    {
        delete s;
        s = nullptr;
    }

    if (app)   log_screens_[0] = new LogScreen(*app,   "2: App Log");
    if (svc)   log_screens_[1] = new LogScreen(*svc,   "3: Svc Log");
    if (debug) log_screens_[2] = new LogScreen(*debug, "4: Debug Log");
    if (esp)   log_screens_[3] = new LogScreen(*esp,   "5: ESP Log");
}

// ---------------------------------------------------------------------------
// IsServiceInterface
// ---------------------------------------------------------------------------

std::string AmakerBotUIService::getServiceName()
{
    return progmem_to_string(AmakerBotUIConsts::str_service_name);
}

bool AmakerBotUIService::initializeService()
{
    if (isServiceInitialized())
        return true;

    previous_screen_ = static_cast<Screen>(0xFF);
    current_screen_  = SCREEN_SPLASH;

    btn_a_prev_      = false;
    btn_a_last_ms_   = 0;

    setServiceStatus(INITIALIZED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_init_ok));
    return true;
}

bool AmakerBotUIService::startService()
{
    if (!isServiceInitialized())
    {
        setServiceStatus(START_FAILED);
        return false;
    }

    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.fillScreen(UIColors::CLR_BLACK);

    setServiceStatus(STARTED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_start_ok),"UI");
    return true;
}

bool AmakerBotUIService::stopService()
{
    tft.fillScreen(UIColors::CLR_BLACK);
    setServiceStatus(STOPPED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_stop_ok));
    return true;
}

// ---------------------------------------------------------------------------
// Screen navigation
// ---------------------------------------------------------------------------

void AmakerBotUIService::nextScreen()
{
    current_screen_ = static_cast<Screen>(
        (static_cast<uint8_t>(current_screen_) + 1) % SCREEN_COUNT);
    if (current_screen_ == SCREEN_SPLASH)
        splash_start_ms_ = millis();
}

void AmakerBotUIService::setScreen(Screen s)
{
    if (static_cast<uint8_t>(s) < SCREEN_COUNT)
        current_screen_ = s;
}

IsScreen *AmakerBotUIService::activeScreen()
{
    if (current_screen_ == SCREEN_SPLASH)
        return &splash_screen_;

    if (current_screen_ == SCREEN_APP_INFO)
        return &app_screen_;

    const int idx = static_cast<int>(current_screen_) - 2;
    if (idx >= 0 && idx < 4)
        return log_screens_[idx]; // may be nullptr if logger was not attached

    return nullptr;
}

// ---------------------------------------------------------------------------
// tick()
// ---------------------------------------------------------------------------

void AmakerBotUIService::tick()
{
    if (!isServiceStarted())
        return;

    pollButtonA();

    // Auto-advance past splash screen after timeout
    if (current_screen_ == SCREEN_SPLASH &&
        (millis() - splash_start_ms_ >= AmakerBotUIConsts::SPLASH_MAX_MS))
    {
        nextScreen();
    }

    const bool screen_changed = (current_screen_ != previous_screen_);

    if (screen_changed)
    {
        tft.fillScreen(UIColors::CLR_BLACK);
        previous_screen_ = current_screen_;

        IsScreen *scr = activeScreen();
        if (scr)
            scr->initScreen();
    }

    if (!screen_changed && !needsRedraw())
        return;

    IsScreen *scr = activeScreen();
    if (scr)
        scr->updateScreen();
}

// ---------------------------------------------------------------------------
// needsRedraw()
// ---------------------------------------------------------------------------

bool AmakerBotUIService::needsRedraw() const
{
    if (current_screen_ == SCREEN_SPLASH)
        return false; // static image, no periodic redraw needed

    if (current_screen_ == SCREEN_APP_INFO)
        return app_screen_.needsUpdate();

    const int idx = static_cast<int>(current_screen_) - 2;
    if (idx >= 0 && idx < 4 && log_screens_[idx])
        return log_screens_[idx]->needsUpdate();

    return false;
}

// ---------------------------------------------------------------------------
// pollButtonA()
// ---------------------------------------------------------------------------

void AmakerBotUIService::pollButtonA()
{
    if (!k10_.buttonA)
        return;

    const bool pressed      = k10_.buttonA->isPressed();
    const unsigned long now = millis();

    // Rising edge + debounce
    if (pressed && !btn_a_prev_ &&
        (now - btn_a_last_ms_ >= AmakerBotUIConsts::BTN_A_DEBOUNCE_MS))
    {
        btn_a_last_ms_ = now;
        nextScreen();
    }
    btn_a_prev_ = pressed;
}

// ---------------------------------------------------------------------------
// IsBotActionHandlerInterface
// ---------------------------------------------------------------------------

uint8_t AmakerBotUIService::getBotServiceId() const
{
    return AmakerBotUIConsts::UI_SERVICE_ID;
}

std::string AmakerBotUIService::handleBotMessage(const uint8_t *data, size_t len)
{
    if (!data || len < 1)
        return BotProto::make_ack(0x00, BotProto::resp_invalid_params);

    const uint8_t action = data[0];
    const uint8_t cmd    = BotProto::command(action);

    // ---- CMD_NEXT_SCREEN 0x01 : advance to next screen ----
    if (cmd == AmakerBotUIConsts::CMD_NEXT_SCREEN)
    {
        nextScreen();
        if (debugLogger)
            debugLogger->debug(getServiceName() + " next screen");
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    // ---- CMD_PREV_SCREEN 0x02 : go back to previous screen ----
    if (cmd == AmakerBotUIConsts::CMD_PREV_SCREEN)
    {
        // Go back: equivalent to nextScreen() SCREEN_COUNT-1 times
        current_screen_ = static_cast<Screen>(
            (static_cast<uint8_t>(current_screen_) + SCREEN_COUNT - 1) % SCREEN_COUNT);
        if (current_screen_ == SCREEN_SPLASH)
            splash_start_ms_ = millis();
        if (debugLogger)
            debugLogger->debug(getServiceName() + " previous screen");
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    // ---- CMD_SET_SCREEN 0x03 : [screen_index] ----
    if (cmd == AmakerBotUIConsts::CMD_SET_SCREEN)
    {
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t screen_idx = data[1];
        if (screen_idx >= SCREEN_COUNT)
            return BotProto::make_ack(action, BotProto::resp_invalid_values);

        setScreen(static_cast<Screen>(screen_idx));
        if (current_screen_ == SCREEN_SPLASH)
            splash_start_ms_ = millis();
        if (debugLogger)
            debugLogger->debug(getServiceName() + " set screen: " +
                             std::to_string(screen_idx));
        return BotProto::make_ack(action, BotProto::resp_ok);
    }

    return BotProto::make_ack(action, BotProto::resp_unknown_cmd);
}