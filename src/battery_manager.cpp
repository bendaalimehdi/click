#include "battery_manager.h"

BatteryManager::BatteryManager(int adcPin, const BatteryConfig& cfg)
    : _adcPin(adcPin), _cfg(cfg) {}

void BatteryManager::begin() {
    pinMode(_adcPin, INPUT);
    analogReadResolution(12);
}

float BatteryManager::readVoltage(uint8_t samples) {
    if (_adcPin < 0) {
        return NAN;
    }

    uint32_t sum = 0;
    uint8_t validSamples = 0;

    for (uint8_t i = 0; i < samples; i++) {
        int rawSample = analogRead(_adcPin);
        if (rawSample >= 0) {
            sum += (uint32_t)rawSample;
            validSamples++;
        }
        delay(2);
    }

    if (validSamples == 0) {
        return NAN;
    }

    float raw = (float)sum / (float)validSamples;
    float adcVoltage = (raw / _cfg.adc_max) * _cfg.adc_vref;
    float dividerRatio = (_cfg.divider_r1 + _cfg.divider_r2) / _cfg.divider_r2;

    return adcVoltage * dividerRatio * _cfg.calibration_factor;
}