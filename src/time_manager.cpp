#include "time_manager.h"
#include <time.h>

bool TimeManager::begin(const String& timezone) {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
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

uint32_t TimeManager::secondsUntilNextSlot(const std::vector<String>& reportTimes) {
    time_t now = time(nullptr);
    if (now < 100000) {
        return 12UL * 3600UL;
    }

    struct tm localNow;
    localtime_r(&now, &localNow);

    time_t best = 0;
    bool found = false;

    for (const auto& slot : reportTimes) {
        int h, m;
        if (!parseHHMM(slot, h, m)) continue;

        struct tm candidate = localNow;
        candidate.tm_hour = h;
        candidate.tm_min = m;
        candidate.tm_sec = 0;

        time_t candidateTs = mktime(&candidate);
        if (candidateTs <= now) {
            candidate.tm_mday += 1;
            candidateTs = mktime(&candidate);
        }

        if (!found || candidateTs < best) {
            best = candidateTs;
            found = true;
        }
    }

    if (!found) return 12UL * 3600UL;

    uint32_t delta = (uint32_t)(best - now);
    if (delta == 0) delta = 1;
    return delta;
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