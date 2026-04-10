#pragma once

#include <Arduino.h>
#include <vector>

class TimeManager {
public:
    bool begin(const String& timezone);
    bool isTimeSynced(uint32_t timeoutMs = 15000);
    uint32_t secondsUntilNextSlot(const std::vector<String>& reportTimes);
    String nowString();
    uint32_t getNow();

private:
    bool parseHHMM(const String& str, int& h, int& m);
};