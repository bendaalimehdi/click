#pragma once
#include <Arduino.h>

class ErrorTracker {
public:
    void setLastError(const String& err) {
        _lastError = err;
        _lastErrorAtMs = millis();
    }

    void clear() {
        _lastError = "";
        _lastErrorAtMs = 0;
    }

    String getLastError() const {
        return _lastError;
    }

    uint32_t getLastErrorAtMs() const {
        return _lastErrorAtMs;
    }

    bool hasError() const {
        return !_lastError.isEmpty();
    }

private:
    String _lastError;
    uint32_t _lastErrorAtMs = 0;
};