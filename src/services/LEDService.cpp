/**
 * @file LEDService.cpp
 * @brief Unified RGB LED control for the 3 K10 NeoPixels and
 *        2 DFR1216 WS2812 LEDs.
 *
 * @note  `DFR1216Board::setLEDColor()` zeros unselected LEDs each call, so
 *        LEDService maintains its own full-state cache for the DFR LEDs and
 *        calls `board_.setWS2812()` directly via flushDFRLeds(), which always
 *        sends both LED entries in a single I2C transaction.
 */

#include "services/LEDService.h"
#include "FlashStringHelper.h"
#include <cstring>

namespace
{
    constexpr const char logger_source[] = "LED";
}

#define trace(message, ...) trace(message, logger_source)
#define debug(message, ...) debug(message, logger_source)
#define info(message, ...) info(message, logger_source)
#define warning(message, ...) warning(message, logger_source)
#define error(message, ...) error(message, logger_source)

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LEDService::LEDService(UNIHIKER_K10 &k10, DFR1216Board &board)
    : k10_(k10), board_(board)
{
    memset(states_, 0, sizeof(states_));
}

// ---------------------------------------------------------------------------
// IsServiceInterface
// ---------------------------------------------------------------------------

std::string LEDService::getServiceName()
{
    return progmem_to_string(LEDConsts::str_service_name);
}

bool LEDService::initializeService()
{
    if (isServiceInitialized())
        return true;

    memset(states_, 0, sizeof(states_));

    // Turn off all K10 NeoPixels
    if (k10_.rgb)
    {
        k10_.rgb->brightness(0);
        for (uint8_t i = 0; i < LEDConsts::K10_LED_COUNT; i++)
            k10_.rgb->write(i, 0, 0, 0);
    }

    setServiceStatus(INITIALIZED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_init_ok));
    return true;
}

bool LEDService::startService()
{
    if (!isServiceInitialized())
    {
        setServiceStatus(START_FAILED);
        return false;
    }

    if (!board_.isServiceStarted())
        debugLogger->error(FPSTR(LEDConsts::msg_board_not_started));

    setServiceStatus(STARTED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_start_ok));
    return true;
}

bool LEDService::stopService()
{
    turnOffAll();
    setServiceStatus(STOPPED);
    debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_stop_ok));
    return true;
}

// ---------------------------------------------------------------------------
// IsBotActionHandlerInterface
// ---------------------------------------------------------------------------

uint8_t LEDService::getBotServiceId() const
{
    return LEDConsts::BOT_SERVICE_ID;
}

std::string LEDService::handleBotMessage(const uint8_t *data, size_t len)
{
    if (!data || len < 1)
        return BotProto::make_ack(0x00, BotProto::resp_invalid_params);

    const uint8_t action = data[0];
    const uint8_t cmd    = BotProto::command(action);

    // ---- CMD_SET_COLOR  0x01 : [led_mask][r][g][b][brightness] ----
    if (cmd == LEDConsts::CMD_SET_COLOR)
    {
        if (len < 6)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t mask       = data[1];
        const uint8_t r          = data[2];
        const uint8_t g          = data[3];
        const uint8_t b          = data[4];
        const uint8_t brightness = data[5];

        if ((mask & LEDConsts::MASK_ALL) == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t rc = setColor(mask, r, g, b, brightness);
        return BotProto::make_ack(action, rc);
    }

    // ---- CMD_TURN_OFF  0x02 : [led_mask] ----
    if (cmd == LEDConsts::CMD_TURN_OFF)
    {
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t mask = data[1];
        if ((mask & LEDConsts::MASK_ALL) == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t rc = turnOff(mask);
        return BotProto::make_ack(action, rc);
    }

    // ---- CMD_TURN_OFF_ALL  0x03 : (no payload) ----
    if (cmd == LEDConsts::CMD_TURN_OFF_ALL)
    {
        const uint8_t rc = turnOffAll();
        return BotProto::make_ack(action, rc);
    }

    // ---- CMD_GET_COLOR  0x04 : [led_mask]  reply: [led_mask][r][g][b][br]… ----
    if (cmd == LEDConsts::CMD_GET_COLOR)
    {
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        const uint8_t mask = data[1];
        if ((mask & LEDConsts::MASK_ALL) == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        // Collect states into a stack buffer
        LEDState out[LEDConsts::TOTAL_LEDS];
        const uint8_t rc = getColor(mask, out);
        if (rc != BotProto::resp_ok)
            return BotProto::make_ack(action, rc);

        // Build response: [action][resp_ok][mask][r₀][g₀][b₀][br₀]…
        std::string reply;
        reply += static_cast<char>(action);
        reply += static_cast<char>(BotProto::resp_ok);
        reply += static_cast<char>(mask);

        const uint8_t masked = static_cast<uint8_t>(mask & LEDConsts::MASK_ALL);
        for (uint8_t bit = 0, slot = 0; bit < LEDConsts::TOTAL_LEDS; ++bit)
        {
            if (masked & (1u << bit))
            {
                reply += static_cast<char>(out[slot].r);
                reply += static_cast<char>(out[slot].g);
                reply += static_cast<char>(out[slot].b);
                reply += static_cast<char>(out[slot].brightness);
                ++slot;
            }
        }
        return reply;
    }

    return BotProto::make_ack(action, BotProto::resp_unknown_cmd);
}

// ---------------------------------------------------------------------------
// Public LED API
// ---------------------------------------------------------------------------

uint8_t LEDService::setColor(uint8_t led_mask, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t brightness)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;

    const uint8_t valid = static_cast<uint8_t>(led_mask & LEDConsts::MASK_ALL);
    if (valid == 0)
        return BotProto::resp_invalid_params;

    // Expand mask → indices
    uint8_t indices[LEDConsts::TOTAL_LEDS];
    const uint8_t count = unpackMask(valid, indices);

    bool k10_dirty = false;
    bool dfr_dirty = false;

    for (uint8_t i = 0; i < count; ++i)
    {
        const uint8_t idx = indices[i];
        states_[idx].r          = r;
        states_[idx].g          = g;
        states_[idx].b          = b;
        states_[idx].brightness = brightness;

        if (idx < LEDConsts::K10_LED_COUNT)
            k10_dirty = true;
        else
            dfr_dirty = true;
    }

    // Flush K10 NeoPixels
    if (k10_dirty && k10_.rgb)
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            const uint8_t idx = indices[i];
            if (idx < LEDConsts::K10_LED_COUNT)
                flushK10Led(idx);
        }
    }

    // Flush DFR1216 WS2812 (one combined I2C write)
    if (dfr_dirty)
        flushDFRLeds();

    return BotProto::resp_ok;
}

uint8_t LEDService::turnOff(uint8_t led_mask)
{
    return setColor(led_mask, 0, 0, 0, 0);
}

uint8_t LEDService::turnOffAll()
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;

    memset(states_, 0, sizeof(states_));

    if (k10_.rgb)
    {
        k10_.rgb->brightness(0);
        for (uint8_t i = 0; i < LEDConsts::K10_LED_COUNT; i++)
            k10_.rgb->write(i, 0, 0, 0);
    }

    flushDFRLeds();

    debugLogger->info(FPSTR(LEDConsts::msg_all_off));
    return BotProto::resp_ok;
}

uint8_t LEDService::getColor(uint8_t led_mask, LEDState *out) const
{
    if (!out)
        return BotProto::resp_invalid_params;

    const uint8_t valid = static_cast<uint8_t>(led_mask & LEDConsts::MASK_ALL);
    if (valid == 0)
        return BotProto::resp_invalid_params;

    uint8_t slot = 0;
    for (uint8_t bit = 0; bit < LEDConsts::TOTAL_LEDS; ++bit)
    {
        if (valid & (1u << bit))
            out[slot++] = states_[bit];
    }
    return BotProto::resp_ok;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void LEDService::flushK10Led(uint8_t idx)
{
    if (!k10_.rgb || idx >= LEDConsts::K10_LED_COUNT)
        return;

    k10_.rgb->brightness(states_[idx].brightness);
    k10_.rgb->write(static_cast<int8_t>(idx),
                    states_[idx].r,
                    states_[idx].g,
                    states_[idx].b);
}

void LEDService::flushDFRLeds()
{
    if (!board_.isServiceStarted())
        return;

    // Build a 2-element packed-color array for setWS2812.
    // DFR1216 uses states_[3] (LED 0) and states_[4] (LED 1).
    uint32_t colors[2] = {0u, 0u};

    for (uint8_t i = 0; i < LEDConsts::DFR_LED_COUNT; ++i)
    {
        const LEDState &st = states_[LEDConsts::K10_LED_COUNT + i];
        colors[i] = (static_cast<uint32_t>(st.r) << 16) |
                    (static_cast<uint32_t>(st.g) << 8)  |
                    static_cast<uint32_t>(st.b);
    }

    // WS2812 on DFR1216 shares one brightness byte — take the higher value
    // so that whichever LED was last set brighter wins.
    const uint8_t bright = (states_[3].brightness >= states_[4].brightness)
                           ? states_[3].brightness
                           : states_[4].brightness;

    board_.setWS2812(colors, bright);
}

uint8_t LEDService::unpackMask(uint8_t mask, uint8_t *out)
{
    uint8_t count = 0;
    for (uint8_t bit = 0; bit < LEDConsts::TOTAL_LEDS; ++bit)
    {
        if (mask & (1u << bit))
            out[count++] = bit;
    }
    return count;
}
