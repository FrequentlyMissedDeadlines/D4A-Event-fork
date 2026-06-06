/**
 * @file BotServerUDP.cpp
 * @brief Implementation of BotServerUDP — thin UDP transport for the bot protocol.
 */

#include "BotCommunication/BotServerUDP.h"
#include "RollingLogger.h"
#include "FlashStringHelper.h"
#include <Arduino.h>    // FPSTR()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BotServerUDP::BotServerUDP(AmakerBotService &bot, uint16_t port)
    : bot_(bot), port_(port)
{
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool BotServerUDP::start()
{
    udp_ = new AsyncUDP();
    if (!udp_)
    {
        if (logger_)
            logger_->error(FPSTR(BotServerUDPConsts::msg_no_udp));
        return false;
    }

    // Register the onPacket callback as a capturing lambda.
    // BotServerUDP is a long-lived object (typically static in main.cpp),
    // so capturing `this` is safe for the lifetime of the server.
    udp_->onPacket([this](AsyncUDPPacket packet)
    {
        const size_t len = packet.length();
        if (len == 0)
        {
            ++dropped_count_;
            return;
        }

        ++rx_count_;

        // dispatch() is lock-free and safe to call from this callback context.
        // Pass the remote IP so CMD_REGISTER can record the master.
        const std::string senderIP = packet.remoteIP().toString().c_str();
        const std::string response = bot_.dispatch(packet.data(), len, senderIP);

        if (!response.empty())
        {
            sendReply(response, packet.remoteIP(), packet.remotePort());
        }
    });

    if (!udp_->listen(port_))
    {
        if (logger_)
            logger_->error(fpstr_to_string(FPSTR(BotServerUDPConsts::msg_start_failed))
                           + std::to_string(port_));
        delete udp_;
        udp_ = nullptr;
        return false;
    }

    if (logger_)
        logger_->info(fpstr_to_string(FPSTR(BotServerUDPConsts::msg_start_ok))
                      + std::to_string(port_), "UDP");
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void BotServerUDP::stop()
{
    if (!udp_)
        return;

    udp_->close();
    delete udp_;
    udp_ = nullptr;

    if (logger_)
        logger_->info(FPSTR(BotServerUDPConsts::msg_stop));
}

// ---------------------------------------------------------------------------
// sendReply
// ---------------------------------------------------------------------------

bool BotServerUDP::sendReply(const std::string  &data,
                              const IPAddress    &remote_ip,
                              uint16_t            remote_port)
{
    if (!udp_ || data.empty())
        return false;

    const size_t written = udp_->writeTo(
        reinterpret_cast<const uint8_t *>(data.data()),
        data.size(),
        remote_ip,
        remote_port);

    const bool ok = (written == data.size());
    if (ok)
        ++tx_count_;

    return ok;
}
