#pragma once

#include <Arduino.h>
#include "config_manager.h"

class BatteryManager {
public:
    BatteryManager(int adcPin, const BatteryConfig& cfg);
    void begin();
    float readVoltage(uint8_t samples = 8);

private:
    int _adcPin;
    BatteryConfig _cfg;
};