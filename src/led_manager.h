#pragma once
#include <Arduino.h>
#include <FastLED.h>

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 8
#endif

class LedManager {
public:
    LedManager(uint8_t pin = STATUS_LED_PIN, uint8_t numPixels = 1);
    void begin();

    void setRed();
    void setGreen();
    void setBlue();
    void setOff();

    void blinkBlue(uint8_t times = 2);
    void tick();

private:
    uint8_t _pin;
    uint8_t _numPixels;

    CRGB _leds[1];

    bool _blinking = false;
    uint8_t _blinkRemaining = 0;
    bool _blinkState = false;
    uint32_t _lastBlinkMs = 0;
    static constexpr uint32_t BLINK_INTERVAL_MS = 120;

    void setColor(uint8_t r, uint8_t g, uint8_t b);
};