#pragma once
#include <Arduino.h>

class Logger {
public:
    static void info(const String& msg) {
        Serial.println("[INFO] " + msg);
    }

    static void warn(const String& msg) {
        Serial.println("[WARN] " + msg);
    }

    static void error(const String& msg) {
        Serial.println("[ERROR] " + msg);
    }

    static void debug(const String& msg) {
        Serial.println("[DEBUG] " + msg);
    }
};