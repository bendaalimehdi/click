#include "leader_state_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool LeaderStateManager::begin() {
    return true;
}

bool LeaderStateManager::saveNodes(const std::vector<NodeRecord>& nodes) {
    JsonDocument doc;
    JsonArray arr = doc["nodes"].to<JsonArray>();

    for (const auto& n : nodes) {
        JsonObject o = arr.add<JsonObject>();
        o["client"] = n.client;
        o["id"] = n.id;
        o["temp"] = n.temp;
        o["volt"] = n.volt;
        o["lastSeenMs"] = n.lastSeenMs;
        o["mac"] = n.mac;
    }

    File f = LittleFS.open(_stateFile, "w");
    if (!f) {
        _lastError = "Failed to open leader_state.json for write";
        return false;
    }

    if (serializeJsonPretty(doc, f) == 0) {
        f.close();
        _lastError = "Failed to write leader state";
        return false;
    }

    f.close();
    return true;
}

bool LeaderStateManager::loadNodes(std::vector<NodeRecord>& nodes) {
    nodes.clear();

    File f = LittleFS.open(_stateFile, "r");
    if (!f) {
        return true;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, f);
    f.close();

    if (err) {
        _lastError = "Failed to parse leader_state.json";
        return false;
    }

    JsonArray arr = doc["nodes"].as<JsonArray>();
    for (JsonVariant v : arr) {
        NodeRecord n;
        n.client = String((const char*)(v["client"] | ""));
        n.id = v["id"] | 0;
        n.temp = v["temp"] | NAN;
        n.volt = v["volt"] | NAN;
        n.lastSeenMs = v["lastSeenMs"] | 0;
        n.mac = String((const char*)(v["mac"] | ""));
        nodes.push_back(n);
    }

    return true;
}

String LeaderStateManager::getLastError() const {
    return _lastError;
}