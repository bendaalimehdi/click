#include "time_manager.h"
#include <time.h>

bool TimeManager::begin(const String& timezone) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    
    return true;
}

bool TimeManager::isTimeSynced(uint32_t timeoutMs) {
    uint32_t start = millis();
    struct tm timeinfo;

    while (millis() - start < timeoutMs) {
        if (getLocalTime(&timeinfo, 500)) {
            return (timeinfo.tm_year > (2020 - 1900));
        }
        delay(250);
    }
    return false;
}

bool TimeManager::parseHHMM(const String& str, int& h, int& m) {
    int sep = str.indexOf(':');
    if (sep <= 0) return false;
    h = str.substring(0, sep).toInt();
    m = str.substring(sep + 1).toInt();
    return h >= 0 && h < 24 && m >= 0 && m < 60;
}

uint32_t TimeManager::secondsUntilNextInterval(uint16_t intervalMin) {
    time_t now = time(nullptr);
    if (now < 100000) {
        return intervalMin * 60; // fallback si pas sync
    }

    uint32_t nowSec = (uint32_t)now;
    uint32_t intervalSec = intervalMin * 60;

    uint32_t next = ((nowSec / intervalSec) + 1) * intervalSec;

    return next - nowSec;
}

String TimeManager::nowString() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
}

uint32_t TimeManager::getNow() {
    return (uint32_t)time(nullptr);
}