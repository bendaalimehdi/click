#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <functional>
#include "protocol_types.h"

class EspNowManager {
public:
    using SensorCallback = std::function<void(const uint8_t* mac, const SensorData&)>;
    using SyncCallback   = std::function<void(const uint8_t* mac, const SyncData&)>;

    bool begin();
    bool addPeer(const uint8_t* mac, uint8_t channel = 0, bool encrypt = false);
    bool addPeerFromString(const String& macStr, uint8_t channel = 0, bool encrypt = false);
    bool sendSensorData(const uint8_t* mac, const SensorData& data);
    bool sendSyncData(const uint8_t* mac, const SyncData& data);
    void onSensorReceived(SensorCallback cb);
    void onSyncReceived(SyncCallback cb);

    static bool macStringToBytes(const String& macStr, uint8_t out[6]);
    static String macBytesToString(const uint8_t* mac);

private:
    // Signature obligatoire pour ESP32-C6 / Arduino 3.0+
    static void onDataRecv(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len);
    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);

    static EspNowManager* _instance;
    SensorCallback _sensorCb;
    SyncCallback _syncCb;
};