#include "follower_service.h"
#include "logger.h"
#include <WiFi.h>
#include <esp_sleep.h>

FollowerService::FollowerService(const AppConfig& cfg)
    : _cfg(cfg),
      _sensor(cfg.gpio_pins.i2c_sda, cfg.gpio_pins.i2c_scl, cfg.sensor_type),
      _battery(cfg.gpio_pins.battery_adc, cfg.battery) {}

bool FollowerService::beginAndSleep() {
    uint32_t bootMs = millis();

    if (!_sensor.begin()) {
        Logger::warn("Sensor init failed: " + _sensor.getLastError());
    }

    _battery.begin();

    float temp = _sensor.readTemperatureC();
    float volt = _battery.readVoltage();

    Logger::info("Follower boot temp=" + String(temp, 2) + " volt=" + String(volt, 2));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (!_espnow.begin()) {
        Logger::error("Follower: ESP-NOW init failed");
        enterDeepSleep(12UL * 3600UL);
        return false;
    }

    uint8_t leaderMac[6];
    if (!EspNowManager::macStringToBytes(_cfg.leader_mac, leaderMac)) {
        Logger::error("Follower: invalid leader MAC");
        enterDeepSleep(12UL * 3600UL);
        return false;
    }

    if (!_espnow.addPeer(leaderMac)) {
        Logger::warn("Follower: addPeer failed or peer not accepted");
    }

    _espnow.onSyncReceived([this](const uint8_t* mac, const SyncData& packet) {
        (void)mac;
        _syncData = packet;
        _syncReceived = true;
    });

    SensorData payload = {};
    strlcpy(payload.client, _cfg.device_name.c_str(), sizeof(payload.client));
    payload.id = _cfg.id;
    payload.temp = temp;
    payload.volt = volt;
    payload.count = 1; // Ou un compteur stocké en RTC
    payload.rssi = WiFi.RSSI();

    bool sent = _espnow.sendSensorData(leaderMac, payload);
    Logger::info(sent ? "SensorData sent" : "SensorData send failed");

    waitForSync(_cfg.follower_wait_sync_ms);

    uint32_t awake = millis() - bootMs;
    Logger::info("Awake time ms=" + String(awake));

    enterDeepSleep(_syncReceived ? _syncData.next_sleep_seconds : 12UL * 3600UL);
    return true;
}

bool FollowerService::waitForSync(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (_syncReceived) {
            Logger::info("Sync received sleep=" + String(_syncData.next_sleep_seconds));
            return true;
        }
        delay(5);
    }

    Logger::warn("No SyncData received, fallback sleep");
    return false;
}

void FollowerService::enterDeepSleep(uint32_t seconds) {
    if (seconds == 0) seconds = 1;
    Logger::info("Deep sleep seconds=" + String(seconds));
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    esp_deep_sleep_start();
}