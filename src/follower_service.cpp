#include "follower_service.h"
#include "logger.h"
#include "config_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <ArduinoJson.h>

FollowerService::FollowerService(const AppConfig& cfg)
    : _cfg(cfg),
      _sensor(cfg.gpio_pins.i2c_sda, cfg.gpio_pins.i2c_scl, cfg.sensor_type),
      _battery(cfg.gpio_pins.battery_adc, cfg.battery),
      _led() {}
bool FollowerService::beginAndSleep() {
    uint32_t bootMs = millis();

    // 1. Initialisation matérielle de base
    _battery.begin();
    
    // 2. Vérification du mode d'entrée
    // Utilisation du GPIO 9 (Bouton BOOT sur Super Mini C6) pour forcer le setup
    pinMode(9, INPUT_PULLUP);
    bool forceInstall = (digitalRead(9) == LOW);
    
    // Si la configuration est incomplète ou si le bouton est pressé, passer en mode Installation
    if (_cfg.leader_mac.isEmpty() || _cfg.leader_mac == "00:00:00:00:00:00" || forceInstall) {
        return runInstallMode();
    }

    // 3. Mode de fonctionnement normal (RUN_MODE)
    return runNormalMode(bootMs);
}

bool FollowerService::runInstallMode() {
    _led.begin();
    Logger::info(">>> MODE INSTALLATION ACTIF <<<");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // 1. Essayer le canal en cache
    uint8_t channel = _cfg.wifi_channel;
    Logger::info("Install mode: trying cached channel " + String(channel));
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    // 2. Vérifier que l'AP du leader est sur ce canal, sinon scanner
    uint8_t foundChannel = scanForLeaderChannel(_cfg.ap_ssid);
    if (foundChannel != 0 && foundChannel != channel) {
        Logger::info("Install: channel mismatch, switching "
                     + String(channel) + " -> " + String(foundChannel));
        channel = foundChannel;
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        // Sauvegarder immédiatement le bon canal
        _cfg.wifi_channel = channel;
        ConfigManager cm;
        if (cm.begin()) cm.save(_cfg);
    }

    if (!_espnow.begin()) {
        Logger::error("Erreur ESP-NOW en mode Install");
        delay(5000);
        ESP.restart();
        return false;
    }

    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    _espnow.addPeer(broadcastMac, channel, false);

    _espnow.onRawReceived([this](const uint8_t* mac, const uint8_t* data, size_t len) -> void {
        (void)mac;

        if (!data || len == 0) {
            Logger::warn("Provisioning ignore: payload vide");
            return;
        }

        if (data[0] == '{') {
            Logger::info("Provision payload recu: " + String(reinterpret_cast<const char*>(data)));
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);
        if (err) {
            Logger::warn(String("Provisioning ignore: JSON invalide - ") + err.c_str());
            return;
        }

        String type = doc["type"] | "";
        if (type != "provision") {
            return;
        }

        String receivedMac = doc["leader_mac"] | "";
        String receivedName = doc["device_name"] | "";
        String receivedRole = doc["role"] | "follower";

        if (receivedMac.length() < 17 || receivedName.isEmpty()) {
            Logger::error("Provisioning ignore: Donnees invalides");
            return;
        }

        Logger::info("PROVISION VALIDE ! Leader: " + receivedMac);

        this->_cfg.leader_mac = receivedMac;
        this->_cfg.device_name = receivedName;
        this->_cfg.role = receivedRole;

        // Sauvegarder aussi le canal reçu du leader si présent
        if (doc["channel"].is<int>()) {
            uint8_t leaderChannel = (uint8_t)(int)doc["channel"];
            Logger::info("Channel recu du leader: " + String(leaderChannel));
            this->_cfg.wifi_channel = leaderChannel;
        }

        ConfigManager cm;
        if (cm.begin() && cm.save(this->_cfg)) {
            Logger::info("Config sauvegardee. Reboot...");
            delay(2000);
            ESP.restart();
        } else {
            Logger::error("Echec sauvegarde config");
        }
    });

    uint32_t lastBeacon = 0;
    while (true) {
        if (millis() - lastBeacon > 3000) {
            lastBeacon = millis();
            _led.blinkBlue(2);

            PairingHello hello = {};
            hello.type = MSG_PAIRING_REQ;
            strlcpy(hello.uid, WiFi.macAddress().c_str(), sizeof(hello.uid));
            strlcpy(hello.firmware, "1.1.0", sizeof(hello.firmware));
            strlcpy(hello.sensor_type, _cfg.sensor_type.c_str(), sizeof(hello.sensor_type));
            hello.battery_voltage = _battery.readVoltage();

            _espnow.sendRaw(broadcastMac, (const uint8_t*)&hello, sizeof(hello));
            Logger::info("PairingHello sent");
        }
        _led.tick();
        delay(10);
    }

    return true;
}
bool FollowerService::runNormalMode(uint32_t bootMs) {
    if (!_sensor.begin()) {
        Logger::warn("Sensor init failed: " + _sensor.getLastError());
    }

    float temp = _sensor.readTemperatureC();
    float volt = _battery.readVoltage();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100); // laisser le wifi se stabiliser

    // 1. Déterminer le canal — AVANT esp_now init
    uint8_t channel = _cfg.wifi_channel;
    Logger::info("Trying cached channel: " + String(channel));

    // Scan préventif pour vérifier le canal (rapide, avant ESP-NOW)
    uint8_t foundChannel = scanForLeaderChannel(_cfg.ap_ssid);
    if (foundChannel != 0 && foundChannel != channel) {
        Logger::info("Channel updated: " + String(channel) + " -> " + String(foundChannel));
        channel = foundChannel;
        _cfg.wifi_channel = channel;
        ConfigManager cm;
        if (cm.begin()) cm.save(_cfg);
    }

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    // 2. Init ESP-NOW après avoir fixé le canal
    if (!_espnow.begin()) {
        Logger::error("Follower: ESP-NOW init failed");
        enterDeepSleep(60UL);
        return false;
    }

    uint8_t leaderMac[6];
    if (!EspNowManager::macStringToBytes(_cfg.leader_mac, leaderMac)) {
        Logger::error("Follower: invalid leader MAC");
        enterDeepSleep(60UL);
        return false;
    }

    _espnow.addPeer(leaderMac, channel, false);

    _espnow.onSyncReceived([this](const uint8_t* mac, const SyncData& packet) {
        (void)mac;
        _syncData = packet;
        _syncReceived = true;
    });

    SensorData payload = {};
    payload.type = MSG_SENSOR_DATA;
    strlcpy(payload.client, _cfg.device_name.c_str(), sizeof(payload.client));
    payload.id = _cfg.id;
    payload.temp = temp;
    payload.volt = volt;
    payload.count = 1;
    payload.rssi = WiFi.RSSI();

    bool sent = _espnow.sendSensorData(leaderMac, payload);
    Logger::info(sent ? "SensorData sent" : "SensorData send failed");

    waitForSync(_cfg.follower_wait_sync_ms);

    // Pas de retry nécessaire — le canal était déjà correct avant l'envoi
    if (!_syncReceived) {
        Logger::warn("No sync received after channel verification, fallback sleep");
    }

    uint32_t awake = millis() - bootMs;
    Logger::info("Awake time ms=" + String(awake));
    enterDeepSleep(_syncReceived ? _syncData.next_sleep_seconds : 60UL);
    return true;
}

// Scan uniquement déclenché en fallback
uint8_t FollowerService::scanForLeaderChannel(const String& apSsid) {
    Logger::info("Scanning for AP: " + apSsid);
    int n = WiFi.scanNetworks(false, false, false, 300);
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == apSsid) {
            uint8_t ch = WiFi.channel(i);
            Logger::info("Found on channel " + String(ch));
            WiFi.scanDelete();
            return ch;
        }
    }
    WiFi.scanDelete();
    Logger::warn("AP not found during scan");
    return 0; // 0 = introuvable
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