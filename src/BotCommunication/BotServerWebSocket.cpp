/**
 * @file BotServerWebSocket.cpp
 * @brief Implementation of BotServerWebSocket — thin WebSocket transport
 *        for the binary bot protocol.
 */

#include "BotCommunication/BotServerWebSocket.h"
#include "RollingLogger.h"
#include <Arduino.h>    // FPSTR(), millis()


// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BotServerWebSocket::BotServerWebSocket(AmakerBotService &bot, uint16_t port)
    : bot_(bot), port_(port)
{
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool BotServerWebSocket::start()
{
    server_ = new AsyncWebServer(port_);
    ws_     = new AsyncWebSocket(FPSTR(BotServerWSConsts::ws_path));

    if (!server_ || !ws_)
    {
        if (logger_)
            logger_->error(FPSTR(BotServerWSConsts::msg_no_alloc));
        delete server_; server_ = nullptr;
        delete ws_;     ws_     = nullptr;
        return false;
    }

    // Disable "close connection on queue full" globally for all clients
    // that will connect.  We drop messages instead of killing connections.
    ws_->enable(true);

    attachEventHandler();

    server_->addHandler(ws_);
    server_->begin();

    if (logger_)
        logger_->info(String(FPSTR(BotServerWSConsts::msg_start_ok)).c_str()
                      + std::to_string(port_),"WS");
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void BotServerWebSocket::stop()
{
    if (ws_)
    {
        ws_->closeAll();
        ws_->cleanupClients();
    }
    if (server_)
    {
        server_->end();
        delete server_;
        server_ = nullptr;
    }
    // ws_ is owned by server_ handler list in AsyncWebServer; after end() it
    // is no longer safe to delete independently — set to nullptr only.
    ws_ = nullptr;

    if (logger_)
        logger_->info(FPSTR(BotServerWSConsts::msg_stop));
}

// ---------------------------------------------------------------------------
// attachEventHandler
// ---------------------------------------------------------------------------

void BotServerWebSocket::attachEventHandler()
{
    ws_->onEvent([this](AsyncWebSocket        *server,
                         AsyncWebSocketClient *client,
                         AwsEventType          type,
                         void                 *arg,
                         uint8_t              *data,
                         size_t                len)
    {
        switch (type)
        {
        case WS_EVT_CONNECT:
            // Prevent connection kill on queue-full; drop the frame instead.
            client->setCloseClientOnQueueFull(false);
            if (logger_)
                logger_->info(String(FPSTR(BotServerWSConsts::msg_client_conn)).c_str()
                              + std::to_string(client->id()));
            break;

        case WS_EVT_DISCONNECT:
            if (logger_)
                logger_->info(String(FPSTR(BotServerWSConsts::msg_client_disc)).c_str()
                              + std::to_string(client->id()));
            break;

        case WS_EVT_ERROR:
        {
            if (logger_)
            {
                const uint16_t code = arg ? *reinterpret_cast<uint16_t *>(arg) : 0;
                logger_->error(String(FPSTR(BotServerWSConsts::msg_client_err)).c_str()
                               + std::to_string(client->id())
                               + " code=" + std::to_string(code));
            }
            break;
        }

        case WS_EVT_DATA:
        {
            // Only handle complete single-frame messages.
            // Binary bot messages are small; fragmented frames are not expected.
            const AwsFrameInfo *info = reinterpret_cast<AwsFrameInfo *>(arg);
            if (!info->final || info->index != 0 || info->len != len)
            {
                ++dropped_count_;
                if (logger_)
                    logger_->debug(FPSTR(BotServerWSConsts::msg_frag_dropped));
                break;
            }

            if (len == 0)
            {
                ++dropped_count_;
                break;
            }

            ++rx_count_;

            // Dispatch — lock-free, synchronous.
            // Pass the remote IP so CMD_REGISTER can record the master.
            const std::string senderIP = client->remoteIP().toString().c_str();
            const std::string response = bot_.dispatch(data, len, senderIP);

            if (!response.empty())
                sendReply(client->id(), response);

            break;
        }

        default:
            break;
        }
    });
}

// ---------------------------------------------------------------------------
// sendReply
// ---------------------------------------------------------------------------

bool BotServerWebSocket::sendReply(uint32_t client_id, const std::string &data)
{
    if (!ws_ || data.empty())
        return false;

    if (!ws_->hasClient(client_id))
        return false;

    if (!ws_->availableForWrite(client_id))
        return false;

    ws_->binary(client_id,
                reinterpret_cast<const uint8_t *>(data.data()),
                data.size());
    ++tx_count_;
    return true;
}

// ---------------------------------------------------------------------------
// broadcast
// ---------------------------------------------------------------------------

void BotServerWebSocket::broadcast(const std::string &data)
{
    if (!ws_ || data.empty())
        return;

    ws_->binaryAll(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

// ---------------------------------------------------------------------------
// cleanupClients
// ---------------------------------------------------------------------------

void BotServerWebSocket::cleanupClients()
{
    if (ws_)
        ws_->cleanupClients();
}
