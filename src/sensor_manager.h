#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

class SensorManager {
public:
    SensorManager(int sda, int scl, const String& sensorType);
    bool begin();
    float readTemperatureC();
    String getLastError() const;

private:
    int _sda;
    int _scl;
    String _sensorType;
    Adafruit_BMP280 _bmp;
    bool _ready = false;
    String _lastError;
};