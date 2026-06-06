/**
 * @file BotServerBLE.cpp
 * @brief Implementation of BotServerBLE — BLE NUS transport for the bot protocol.
 */

#include "BotCommunication/BotServerBLE.h"
#include "RollingLogger.h"
#include "FlashStringHelper.h"
#include <Arduino.h>    // FPSTR()

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BotServerBLE::BotServerBLE(AmakerBotService &bot)
    : bot_(bot)
{
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

bool BotServerBLE::start()
{
    NimBLEDevice::init(device_name_);

    // Disable all security / pairing requirements.  Without this, hosts with
    // Secure-Connections enforced (e.g. BlueZ with "secure-conn" active) will
    // automatically attempt LE pairing on connect; with no IO capability on this
    // device the pairing fails and BlueZ drops the link ~300 ms after connect.
    NimBLEDevice::setSecurityAuth(false, false, false); // bonding=off, MITM=off, SC=off
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    ble_server_ = NimBLEDevice::createServer();
    if (!ble_server_)
    {
        if (logger_)
            logger_->error(FPSTR(BotServerBLEConsts::msg_no_alloc));
        return false;
    }

    // Register connection / disconnection callbacks (this implements
    // NimBLEServerCallbacks).
    ble_server_->setCallbacks(this);

    // ---- Build Nordic UART Service ----
    NimBLEService *nus = ble_server_->createService(
        BotServerBLEConsts::nus_service_uuid);

    // TX characteristic — notify, bot → client
    tx_char_ = nus->createCharacteristic(
        BotServerBLEConsts::nus_tx_char_uuid,
        NIMBLE_PROPERTY::NOTIFY);

    // RX characteristic — write (no response), client → bot
    NimBLECharacteristic *rx_char = nus->createCharacteristic(
        BotServerBLEConsts::nus_rx_char_uuid,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);

    // Register write callback (this implements NimBLECharacteristicCallbacks).
    rx_char->setCallbacks(this);

    nus->start();

    // ---- Advertising ----
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BotServerBLEConsts::nus_service_uuid);
    adv->setScanResponse(true);           // include full name in scan response
    adv->start();

    running_ = true;

    if (logger_)
        logger_->info(fpstr_to_string(FPSTR(BotServerBLEConsts::msg_start_ok))
                      + device_name_,"BLE");
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

void BotServerBLE::stop()
{
    if (!running_)
        return;

    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);   // true = clear all service / char objects

    ble_server_ = nullptr;
    tx_char_    = nullptr;
    running_    = false;

    if (logger_)
        logger_->info(FPSTR(BotServerBLEConsts::msg_stop));
}

// ---------------------------------------------------------------------------
// sendReply
// ---------------------------------------------------------------------------

bool BotServerBLE::sendReply(const std::string &data)
{
    if (!running_ || !tx_char_ || data.empty())
        return false;

    // Only notify if at least one client has subscribed (CCCD enabled).
    if (tx_char_->getSubscribedCount() == 0)
        return false;

    tx_char_->setValue(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    tx_char_->notify();
    ++tx_count_;
    return true;
}

// ---------------------------------------------------------------------------
// NimBLEServerCallbacks — onConnect
// ---------------------------------------------------------------------------

void BotServerBLE::onConnect(NimBLEServer * /*server*/, ble_gap_conn_desc *desc)
{
    if (logger_)
        logger_->info(fpstr_to_string(FPSTR(BotServerBLEConsts::msg_client_conn))
                      + NimBLEAddress(desc->peer_ota_addr).toString());

    // NOTE: Do NOT call updateConnParams() here. Sending a connection-parameter
    // update request during the host's GATT service-discovery phase causes
    // BlueZ (Linux) to drop the link immediately ("failed to discover services").
    // The default connection interval negotiated by NimBLE is acceptable.
}

// ---------------------------------------------------------------------------
// NimBLEServerCallbacks — onDisconnect
// ---------------------------------------------------------------------------

void BotServerBLE::onDisconnect(NimBLEServer * /*server*/, ble_gap_conn_desc *desc)
{
    if (logger_)
        logger_->info(fpstr_to_string(FPSTR(BotServerBLEConsts::msg_client_disc))
                      + NimBLEAddress(desc->peer_ota_addr).toString());

    // Restart advertising so a new client can connect immediately.
    if (running_)
        NimBLEDevice::getAdvertising()->start();
}

// ---------------------------------------------------------------------------
// NimBLECharacteristicCallbacks — onWrite (RX characteristic)
// ---------------------------------------------------------------------------

void BotServerBLE::onWrite(NimBLECharacteristic *characteristic,
                           ble_gap_conn_desc    *desc)
{
    const NimBLEAttValue val = characteristic->getValue();
    const size_t len = val.size();

    if (len == 0)
    {
        ++dropped_count_;
        return;
    }

    ++rx_count_;

    // Build a "sender ID" string from the BLE address (analogous to IP for UDP/WS).
    const std::string sender_id = NimBLEAddress(desc->peer_ota_addr).toString();

    // dispatch() is documented lock-free; safe to call from this NimBLE task.
    const std::string response = bot_.dispatch(
        reinterpret_cast<const uint8_t *>(val.data()), len, sender_id);

    if (!response.empty())
        sendReply(response);
}
