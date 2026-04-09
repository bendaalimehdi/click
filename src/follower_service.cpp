#include "follower_service.h"
#include "logger.h"
#include "config_manager.h"
#include <WiFi.h>
#include <esp_sleep.h>

FollowerService::FollowerService(const AppConfig& cfg)
    : _cfg(cfg),
      _sensor(cfg.gpio_pins.i2c_sda, cfg.gpio_pins.i2c_scl, cfg.sensor_type),
      _battery(cfg.gpio_pins.battery_adc, cfg.battery) {}

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
    Logger::info(">>> MODE INSTALLATION ACTIF <<<");
    Logger::info("En attente de configuration via le Leader...");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (!_espnow.begin()) {
        Logger::error("Erreur ESP-NOW en mode Install");
        delay(5000);
        ESP.restart();
        return false;
    }

    // Callback pour intercepter les messages de provisioning
    // Note : Cette partie nécessite que EspNowManager gère le type MSG_PROVISION
    _espnow.onSyncReceived([this](const uint8_t* mac, const SyncData& sync) {
        // Logique de réception de configuration via ESP-NOW
        // Une fois la config reçue, on sauvegarde et on redémarre
    });

    uint32_t lastBeacon = 0;
    while (true) {
        // Envoi d'un signal "PairingHello" toutes les 3 secondes
        if (millis() - lastBeacon > 3000) {
            lastBeacon = millis();
            
            PairingHello hello;
            strlcpy(hello.uid, WiFi.macAddress().c_str(), sizeof(hello.uid));
            strlcpy(hello.firmware, "1.1.0", sizeof(hello.firmware));
            strlcpy(hello.sensor_type, _cfg.sensor_type.c_str(), sizeof(hello.sensor_type));
            hello.battery_voltage = _battery.readVoltage();

            // Envoi en broadcast (FF:FF:FF:FF:FF:FF) pour être détecté par le Leader
            uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            _espnow.sendSensorData(broadcastMac, *((SensorData*)&hello)); 
            
            Logger::info("PairingHello envoyé pour détection...");
        }

        // Petit clignotement LED optionnel pour indiquer le mode installation
        // yield() pour éviter le watchdog
        delay(10);
    }
}

bool FollowerService::runNormalMode(uint32_t bootMs) {
    if (!_sensor.begin()) {
        Logger::warn("Sensor init failed: " + _sensor.getLastError());
    }

    float temp = _sensor.readTemperatureC();
    float volt = _battery.readVoltage();

    Logger::info("Follower boot temp=" + String(temp, 2) + " volt=" + String(volt, 2));

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

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

    _espnow.addPeer(leaderMac);

    _espnow.onSyncReceived([this](const uint8_t* mac, const SyncData& packet) {
        (void)mac;
        _syncData = packet;
        _syncReceived = true;
    });

    // Préparation du message DATA standard
    SensorData payload = {};
    payload.type = MSG_SENSOR_DATA; // Nouveau champ
    strlcpy(payload.client, _cfg.device_name.c_str(), sizeof(payload.client));
    payload.id = _cfg.id;
    payload.temp = temp;
    payload.volt = volt;
    payload.count = 1;
    payload.rssi = WiFi.RSSI();

    bool sent = _espnow.sendSensorData(leaderMac, payload);
    Logger::info(sent ? "SensorData sent" : "SensorData send failed");

    waitForSync(_cfg.follower_wait_sync_ms);

    uint32_t awake = millis() - bootMs;
    Logger::info("Awake time ms=" + String(awake));

    enterDeepSleep(_syncReceived ? _syncData.next_sleep_seconds : 60UL);
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