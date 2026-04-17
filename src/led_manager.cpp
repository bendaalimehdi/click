#include "led_manager.h"

LedManager::LedManager(uint8_t pin, uint8_t numPixels)
    : _pin(pin), _numPixels(numPixels) {}

void LedManager::begin() {
    FastLED.addLeds<WS2812, STATUS_LED_PIN, GRB>(_leds, 1);
    FastLED.setBrightness(40);
    _baseColor = CRGB::Black;
    _leds[0] = _baseColor;
    FastLED.show();
}

void LedManager::applyColor(const CRGB& color) {
    _leds[0] = color;
    FastLED.show();
}

void LedManager::setBaseColor(const CRGB& color) {
    _baseColor = color;
    if (!_blinking) {
        applyColor(_baseColor);
    }
}

void LedManager::setRed()   { _blinking = false; setBaseColor(CRGB::Red); }
void LedManager::setGreen() { _blinking = false; setBaseColor(CRGB::Green); }
void LedManager::setBlue()  { _blinking = false; setBaseColor(CRGB::Blue); }
void LedManager::setOff()   { _blinking = false; setBaseColor(CRGB::Black); }

void LedManager::blinkBlue(uint8_t times) {
    _blinkRemaining = times * 2;
    _blinkState = true;
    _blinking = true;
    _lastBlinkMs = millis();
    applyColor(CRGB::Blue);
}

void LedManager::tick() {
    if (!_blinking) return;
    if (millis() - _lastBlinkMs < BLINK_INTERVAL_MS) return;

    _lastBlinkMs = millis();
    _blinkRemaining--;

    if (_blinkRemaining == 0) {
        _blinking = false;
        applyColor(_baseColor);   // retour à l’état Wi-Fi
        return;
    }

    _blinkState = !_blinkState;
    applyColor(_blinkState ? CRGB::Blue : _baseColor);
}