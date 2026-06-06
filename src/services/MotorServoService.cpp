/**
 * @file MotorServoService.cpp
 * @brief Implementation of MotorServoService.
 *
 * @details Bridges the BotMessageHandler binary protocol to DFR1216Board
 *          motor and servo hardware methods.
 */
#include "services/MotorServoService.h"
#include "FlashStringHelper.h"
#include <Arduino.h>
#include <cstdlib> // abs()
#include <cstring> // memset()

#define MOTOR_SERVO_SERVICE_DEBUG  // (un)comment to (dis)enable debug logging

/// The DFR1216_I2C singleton is owned by DFR1216.cpp; reference it here.
extern DFR1216_I2C board;
// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MotorServoService::MotorServoService() = default;

// ---------------------------------------------------------------------------
// IsServiceInterface
// ---------------------------------------------------------------------------

std::string MotorServoService::getServiceName()
{
    return progmem_to_string(MotorServoConsts::str_service_name);
}

bool MotorServoService::initializeService()
{
    memset(motor_speeds_, 0, sizeof(motor_speeds_));
    memset(servo_angles_, 0, sizeof(servo_angles_));
    for (uint8_t i = 0; i < MotorServoConsts::SERVO_COUNT; ++i)
        servo_types_[i] = ServoType::SERVO_180;
    setServiceStatus(INITIALIZED);
    if (debugLogger)
        debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_init_ok));
    return true;
}

bool MotorServoService::startService()
{
    if (board.getStatus() != STARTED)
    {
        setServiceStatus(START_FAILED);
        if (debugLogger)
            debugLogger->error(progmem_to_string(MotorServoConsts::msg_board_not_started).c_str());
        return false;
    }

    setServiceStatus(STARTED);
    if (debugLogger)
        debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_start_ok));
    return true;
}

bool MotorServoService::stopService()
{
    if (isServiceStarted())
        stopAllMotors();
    setServiceStatus(STOPPED);
    if (debugLogger)
        debugLogger->info(getServiceName() + " " + FPSTR(ServiceConst::msg_stop_ok));
    return true;
}

// ---------------------------------------------------------------------------
// IsBotActionHandlerInterface
// ---------------------------------------------------------------------------

uint8_t MotorServoService::getBotServiceId() const
{
    return MotorServoConsts::BOT_SERVICE_ID;
}

std::string MotorServoService::handleBotMessage(const uint8_t *data, size_t len)
{
    if (len < 1)
        return BotProto::make_ack(0x00, BotProto::resp_invalid_params);

    const uint8_t action = data[0];
    const uint8_t cmd = BotProto::command(action);

    if (!isServiceStarted())
        return BotProto::make_ack(action, BotProto::resp_not_started);

#ifdef MOTOR_SERVO_SERVICE_DEBUG
    // Convert data to hex string format
    std::string hex_data;
    for (size_t i = 0; i < len; ++i)
    {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        // if (i > 0)             hex_data += " ";
        hex_data += buf;
    }
    serviceLogger->info(("RX " + hex_data).c_str(), "motor");
#endif

    std::string resp;
    resp.reserve(16);

    switch (cmd)
    {
    // ------------------------------------------------------------------
    // 0x21  SET_MOTORS_SPEED
    // Frame: [0x21][motor_mask:u8][speed:i8]
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_SET_MOTORS_SPEED:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("SET_MOTORS_SPEED", "motor");
#endif
        if (len < 3)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t motor_mask = data[1];
        if (motor_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const int8_t speed = static_cast<int8_t>(data[2]);
        return BotProto::make_ack(action, setMotorsSpeed(motor_mask, speed));
    }

    // ------------------------------------------------------------------
    // 0x23  SET_SERVOS_SPEED  (CONTINUOUS channels only)
    // Frame: [0x23][servo_mask:u8][speed:i8]
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_SET_SERVOS_SPEED:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("SET_SERVOS_SPEED", "motor");
#endif
        if (len < 3)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t servo_mask = data[1];
        if (servo_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const int8_t speed = static_cast<int8_t>(data[2]);
        return BotProto::make_ack(action, setServosSpeed(servo_mask, speed));
    }
    // ------------------------------------------------------------------
    // 0x24  SET_SERVOS_ANGLE  (positional channels only)
    // Frame: [0x24][servo_mask:u8][angle_hi:u8][angle_lo:u8]  (big-endian i16)
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_SET_SERVOS_ANGLE:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("SET_SERVOS_ANGLE", "motor");
#endif
        if (len < 4)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t servo_mask = data[1];
        if (servo_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const int16_t angle = static_cast<int16_t>(
            (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]));
        return BotProto::make_ack(action, setServosAngle(servo_mask, angle));
    }

    // ------------------------------------------------------------------
    // 0x25  INCREMENT_SERVOS_ANGLE  (positional channels only)
    // Frame: [0x25][servo_mask:u8][delta_hi:u8][delta_lo:u8]  (big-endian i16)
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_INCREMENT_SERVOS_ANGLE:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("INCREMENT_SERVOS_ANGLE", "motor");
#endif
        if (len < 4)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t servo_mask = data[1];
        if (servo_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const int16_t delta = static_cast<int16_t>(
            (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]));
        return BotProto::make_ack(action, incrementServosAngle(servo_mask, delta));
    }

    // ------------------------------------------------------------------
    // 0x28  STOP_ALL_MOTORS
    // Frame: [0x28]
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_STOP_ALL_MOTORS:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("STOP_ALL_MOTORS", "motor");
#endif
        return BotProto::make_ack(action, stopAllMotors());
    }

    // ------------------------------------------------------------------
    // 0x22  SET_SERVO_TYPE
    // Frame: [0x22][servo_mask:u8][type:u8  0=180°,1=270°,2=continuous]
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_SET_SERVO_TYPE:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("SET_SERVO_TYPE", "motor");
#endif
        if (len < 3)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t servo_mask = data[1];
        if (servo_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t raw_type = data[2];
        if (raw_type > 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_values);
        return BotProto::make_ack(action,
                                  setServoTypes(servo_mask, static_cast<ServoType>(raw_type)));
    }

    // ------------------------------------------------------------------
    // 0x26  GET_MOTORS_SPEED
    // Frame:    [0x26][motor_mask:u8]
    // Response: [0x26][ok][motor_mask:u8][speed₀..speedₙ:i8]  (LSB-first order)
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_GET_MOTORS_SPEED:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("GET_MOTORS_SPEED", "motor");
#endif
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t motor_mask = data[1];
        if (motor_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        int8_t speeds[MotorServoConsts::MOTOR_COUNT];
        const uint8_t rc = getMotorSpeeds(motor_mask, speeds);
        if (rc != BotProto::resp_ok)
            return BotProto::make_ack(action, rc);

        resp += static_cast<char>(action);
        resp += static_cast<char>(BotProto::resp_ok);
        resp += static_cast<char>(motor_mask); // echo mask so client can decode positions
        uint8_t count = __builtin_popcount(motor_mask);
        for (uint8_t i = 0; i < count; ++i)
            resp += static_cast<char>(speeds[i]);
        return resp;
    }

    // ------------------------------------------------------------------
    // 0x27  GET_SERVOS_ANGLE
    // Frame:    [0x27][servo_mask:u8]
    // Response: [0x27][ok][servo_mask:u8][ang₀_hi][ang₀_lo]…  (big-endian i16, LSB-first order)
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_GET_SERVOS_ANGLE:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("GET_SERVOS_ANGLE", "motor");
#endif
        if (len < 2)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);
        const uint8_t servo_mask = data[1];
        if (servo_mask == 0)
            return BotProto::make_ack(action, BotProto::resp_invalid_params);

        int16_t angles[MotorServoConsts::SERVO_COUNT];
        const uint8_t rc = getServosAngle(servo_mask, angles);
        if (rc != BotProto::resp_ok)
            return BotProto::make_ack(action, rc);

        resp += static_cast<char>(action);
        resp += static_cast<char>(BotProto::resp_ok);
        resp += static_cast<char>(servo_mask); // echo mask so client can decode positions
        uint8_t count = __builtin_popcount(servo_mask);
        for (uint8_t i = 0; i < count; ++i)
        {
            resp += static_cast<char>((angles[i] >> 8) & 0xFF);
            resp += static_cast<char>(angles[i] & 0xFF);
        }
        return resp;
    }

    // ------------------------------------------------------------------
    // 0x29  GET_BATTERY
    // Frame:    [0x29]
    // Response: [0x29][ok][level:u8  0-100]
    // ------------------------------------------------------------------
    case MotorServoConsts::CMD_GET_BATTERY:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("GET_BATTERY", "motor");
#endif
        const uint8_t level = board.getBattery();
        std::string resp;
        resp.reserve(3);
        resp += static_cast<char>(action);
        resp += static_cast<char>(BotProto::resp_ok);
        resp += static_cast<char>(level);
        return resp;
    }

    default:
    {
#ifdef MOTOR_SERVO_SERVICE_DEBUG
        debugLogger->info("UNKNOWN_COMMAND", "motor");
#endif
        return BotProto::make_ack(action, BotProto::resp_unknown_cmd);
    }
    }
}

// ---------------------------------------------------------------------------
// Motor API
// ---------------------------------------------------------------------------

uint8_t MotorServoService::setMotorsSpeed(uint8_t motor_mask, int8_t speed)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;
    if (motor_mask == 0)
        return BotProto::resp_invalid_params;
    if (speed < MotorServoConsts::SPEED_MIN || speed > MotorServoConsts::SPEED_MAX)
    {
        if (debugLogger)
            debugLogger->error(progmem_to_string(MotorServoConsts::msg_invalid_speed).c_str());
        return BotProto::resp_invalid_values;
    }
    for (uint8_t bit = 0; bit < MotorServoConsts::MOTOR_COUNT; ++bit)
    {
        if (motor_mask & (1u << bit))
            applyMotorSpeed(bit + 1, speed); // bit N maps to motor N+1
    }
    return BotProto::resp_ok;
}

uint8_t MotorServoService::stopAllMotors()
{
    // Note: no isServiceStarted() guard — called from stopService() too
    for (uint8_t m = 1; m <= MotorServoConsts::MOTOR_COUNT; ++m)
        applyMotorSpeed(m, 0);

    if (debugLogger)
        debugLogger->info(progmem_to_string(MotorServoConsts::msg_stopped_all_motors).c_str());
    return BotProto::resp_ok;
}

uint8_t MotorServoService::getMotorSpeeds(uint8_t motor_mask, int8_t *out) const
{
    if (!out || motor_mask == 0)
        return BotProto::resp_invalid_params;
    uint8_t out_idx = 0;
    for (uint8_t bit = 0; bit < MotorServoConsts::MOTOR_COUNT; ++bit)
    {
        if (motor_mask & (1u << bit))
            out[out_idx++] = motor_speeds_[bit]; // bit N maps to motor index N
    }
    return BotProto::resp_ok;
}

// ---------------------------------------------------------------------------
// Servo API
// ---------------------------------------------------------------------------

uint8_t MotorServoService::setServoTypes(uint8_t servo_mask, ServoType type)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;
    if (servo_mask == 0)
        return BotProto::resp_invalid_params;

    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
            servo_types_[bit] = type; // bit N maps to servo channel N
    }
    return BotProto::resp_ok;
}

uint8_t MotorServoService::setServosSpeed(uint8_t servo_mask, int8_t speed)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;
    if (servo_mask == 0)
        return BotProto::resp_invalid_params;
    if (speed < MotorServoConsts::SPEED_MIN || speed > MotorServoConsts::SPEED_MAX)
    {
        if (debugLogger)
            debugLogger->error(progmem_to_string(MotorServoConsts::msg_invalid_speed).c_str());
        return BotProto::resp_invalid_values;
    }
    // Validate all servo channels before touching any hardware
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
        {
            if (servo_types_[bit] != ServoType::CONTINUOUS)
            {
                if (debugLogger)
                    debugLogger->error(progmem_to_string(MotorServoConsts::msg_wrong_servo_type).c_str());
                return BotProto::resp_invalid_params;
            }
        }
    }
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
            applyServoSpeed(bit, speed); // bit N maps to servo channel N
    }
    return BotProto::resp_ok;
}

uint8_t MotorServoService::setServosAngle(uint8_t servo_mask, int16_t angle)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;
    if (servo_mask == 0)
        return BotProto::resp_invalid_params;
    if (angle < MotorServoConsts::ANGLE_MIN || angle > MotorServoConsts::ANGLE_MAX)
    {
        if (debugLogger)
            debugLogger->error(progmem_to_string(MotorServoConsts::msg_invalid_angle).c_str());
        return BotProto::resp_invalid_values;
    }
    // Validate all servo channels before touching any hardware
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
        {
            if (servo_types_[bit] == ServoType::CONTINUOUS)
            {
                if (debugLogger)
                    debugLogger->error(progmem_to_string(MotorServoConsts::msg_wrong_servo_type).c_str());
                return BotProto::resp_invalid_params;
            }
        }
    }
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
        {
            const int16_t clamped = clampToServoRange(servo_types_[bit], angle);
            applyServoAngle(bit, clamped);
        }
    }
    return BotProto::resp_ok;
}

uint8_t MotorServoService::incrementServosAngle(uint8_t servo_mask, int16_t delta)
{
    if (!isServiceStarted())
        return BotProto::resp_not_started;
    if (servo_mask == 0)
        return BotProto::resp_invalid_params;
    if (delta < MotorServoConsts::ANGLE_MIN || delta > MotorServoConsts::ANGLE_MAX)
    {
        if (debugLogger)
            debugLogger->error(progmem_to_string(MotorServoConsts::msg_invalid_angle).c_str());
        return BotProto::resp_invalid_values;
    }
    // Validate all servo channels before touching any hardware
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
        {
            if (servo_types_[bit] == ServoType::CONTINUOUS)
            {
                if (debugLogger)
                    debugLogger->error(progmem_to_string(MotorServoConsts::msg_wrong_servo_type).c_str());
                return BotProto::resp_invalid_params;
            }
        }
    }
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
        {
            const int16_t new_angle = servo_angles_[bit] + delta;
            const int16_t clamped = clampToServoRange(servo_types_[bit], new_angle);
            applyServoAngle(bit, clamped);
        }
    }
    return BotProto::resp_ok;
}

uint8_t MotorServoService::getServosAngle(uint8_t servo_mask, int16_t *out) const
{
    if (!out || servo_mask == 0)
        return BotProto::resp_invalid_params;
    uint8_t out_idx = 0;
    for (uint8_t bit = 0; bit < MotorServoConsts::SERVO_COUNT; ++bit)
    {
        if (servo_mask & (1u << bit))
            out[out_idx++] = servo_angles_[bit]; // bit N maps to servo channel N
    }
    return BotProto::resp_ok;
}

ServoType MotorServoService::getServoType(uint8_t servo_id) const
{
    if (servo_id >= MotorServoConsts::SERVO_COUNT)
        return ServoType::SERVO_180;
    return servo_types_[servo_id];
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void MotorServoService::applyMotorSpeed(uint8_t motor_id, int8_t speed)
{
    // Map 1-based motor ID to forward/backward DFR1216 enum pairs
    eMotorNumber_t fwd_enum, bwd_enum;
    switch (motor_id)
    {
    case 1:
        fwd_enum = eMotor1_A;
        bwd_enum = eMotor1_B;
        break;
    case 2:
        fwd_enum = eMotor2_A;
        bwd_enum = eMotor2_B;
        break;
    case 3:
        fwd_enum = eMotor3_A;
        bwd_enum = eMotor3_B;
        break;
    case 4:
        fwd_enum = eMotor4_A;
        bwd_enum = eMotor4_B;
        break;
    default:
        return;
    }

    const uint16_t duty = static_cast<uint16_t>(
        (static_cast<uint32_t>(abs(static_cast<int>(speed))) * 65535UL) / 100UL);

    // Always zero the opposing H-bridge leg first to prevent shoot-through
    if (speed > 0)
    {
        board.setMotorDuty(bwd_enum, 0);
        board.setMotorDuty(fwd_enum, duty);
    }
    else if (speed < 0)
    {
        board.setMotorDuty(fwd_enum, 0);
        board.setMotorDuty(bwd_enum, duty);
    }
    else // speed == 0: brake both legs
    {
        board.setMotorDuty(fwd_enum, 0);
        board.setMotorDuty(bwd_enum, 0);
    }
    motor_speeds_[motor_id - 1] = speed;
}

void MotorServoService::applyServoAngle(uint8_t servo_id, int16_t angle)
{
    const uint16_t max_angle =
        (servo_types_[servo_id] == ServoType::SERVO_270) ? 270 : 180;
    serviceLogger->info(("Servo " + String(servo_id) + "->" + String(angle)).c_str() , "motor");
    board.setServoAngle(static_cast<eServoNumber_t>(servo_id),
                        static_cast<uint16_t>(angle),
                        max_angle);
    servo_angles_[servo_id] = angle;
}
void MotorServoService::applyServoSpeed(uint8_t servo_id, int8_t speed)
{
    eServo360Direction_t dir;
    if (speed > 0)
        dir = eForward;
    else if (speed < 0)
        dir = eBackward;
    else
        dir = eStop;

    board.setServo360(static_cast<eServoNumber_t>(servo_id),
                      dir,
                      static_cast<uint8_t>(abs(static_cast<int>(speed))));

    // Store speed as int16 in the shared angle cache (used by getServosAngle)
    servo_angles_[servo_id] = static_cast<int16_t>(speed);
}

/*static*/ int16_t MotorServoService::clampToServoRange(ServoType type, int16_t angle)
{
    const int16_t max_angle = (type == ServoType::SERVO_270) ? 270 : 180;
    if (angle < 0)
        return 0;
    if (angle > max_angle)
        return max_angle;
    return angle;
}
