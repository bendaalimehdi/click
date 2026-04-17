#define FASTLED_ESP32_LCD_DRIVER
#include "led_manager.h"

LedManager::LedManager(uint8_t pin, uint8_t numPixels)
    : _pin(pin), _numPixels(numPixels) {}

void LedManager::begin() {
    FastLED.addLeds<WS2812, STATUS_LED_PIN, GRB>(_leds, 1);
    FastLED.setBrightness(40);
    _leds[0] = CRGB::Black;
    FastLED.show();
}

void LedManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _leds[0] = CRGB(r, g, b);
    FastLED.show();
}

void LedManager::setRed()   { _blinking = false; setColor(255, 0, 0); }
void LedManager::setGreen() { _blinking = false; setColor(0, 255, 0); }
void LedManager::setBlue()  { _blinking = false; setColor(0, 0, 255); }
void LedManager::setOff()   { _blinking = false; setColor(0, 0, 0); }

void LedManager::blinkBlue(uint8_t times) {
    _blinkRemaining = times * 2;
    _blinkState = true;
    _blinking = true;
    _lastBlinkMs = millis();
    setColor(0, 0, 255);
}

void LedManager::tick() {
    if (!_blinking) return;
    if (millis() - _lastBlinkMs < BLINK_INTERVAL_MS) return;

    _lastBlinkMs = millis();
    _blinkRemaining--;

    if (_blinkRemaining == 0) {
        _blinking = false;
        setColor(0, 0, 0);
        return;
    }

    _blinkState = !_blinkState;
    _blinkState ? setColor(0, 0, 255) : setColor(0, 0, 0);
}