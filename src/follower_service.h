#pragma once

#include <Arduino.h>
#include "config_manager.h"
#include "sensor_manager.h"
#include "battery_manager.h"
#include "espnow_manager.h"
#include "protocol_types.h"

class FollowerService {
public:
    explicit FollowerService(const AppConfig& cfg);
    bool beginAndSleep();

private:
    // Variables membres manquantes
    AppConfig _cfg;
    SensorManager _sensor;
    BatteryManager _battery;
    EspNowManager _espnow;
    
    volatile bool _syncReceived = false;
    SyncData _syncData{.type = MSG_SYNC_DATA, .next_sleep_seconds = 60UL};

    // Déclarations des méthodes manquantes
    bool runInstallMode();
    bool runNormalMode(uint32_t bootMs);
    bool waitForSync(uint32_t timeoutMs);
    void enterDeepSleep(uint32_t seconds);
};