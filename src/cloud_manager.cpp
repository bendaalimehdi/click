#include "cloud_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "espnow_manager.h"
#include "config_manager.h"
#include "protocol_types.h"

CloudManager::CloudManager(const String& serverUrl, TimeManager& timeMgr)
    : _serverUrl(serverUrl), _time(timeMgr) {} //



// Format unifié : Envoi immédiat (un seul capteur dans la liste)
bool CloudManager::postSensorData(const SensorData& data, const uint8_t* sensorMac, 
                                 const String& leaderMac, const String& leaderName) {
    // 1. Générer le VRAI UUID à partir de la MAC du capteur
    String sMacStr = EspNowManager::macBytesToString(sensorMac);
    String sensorUuid = "";
    for (char c : sMacStr) {
        if (c != ':') sensorUuid += c;
    }

    String effectiveLeaderMac = leaderMac;
    if (effectiveLeaderMac.isEmpty()) {
        effectiveLeaderMac = WiFi.macAddress();
    }

    String lUuid = "";
    for (char c : effectiveLeaderMac) {
        if (c != ':') lUuid += c;
    }

    JsonDocument doc;
    doc["leader_uuid"] = lUuid;
    doc["leader_name"] = leaderName;

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    JsonObject s = sensors.add<JsonObject>();
    
    // 2. Assigner les bonnes valeurs aux bonnes clés
    s["device_uuid"] = sensorUuid;         // L'identifiant MAC unique
    s["device_name"] = String(data.client); // Le nom "FrigoMehdi"
    
    s["temperature_C"] = round(data.temp * 100.0) / 100.0;
    s["battery_voltage"] = (int)(data.volt * 1000);
    s["update_date"] = _time.nowString();

    String payload;
    serializeJson(doc, payload);
    return postJsonPayload(payload);
}
bool CloudManager::postQueuedItem(const QueuedCloudItem& item) {
    struct tm t;
    time_t rawtime = (time_t)item.createdAt;
    localtime_r(&rawtime, &t);
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%dT%H:%M:%S", &t);

    extern AppConfig gConfig; 
   
    String lMacStr = gConfig.leader_mac.isEmpty() ? String(WiFi.macAddress()) : gConfig.leader_mac;
    String lUuid = "";
    for (char c : lMacStr) { if (c != ':') lUuid += c; }

    JsonDocument doc;
    doc["leader_uuid"] = lUuid;
    doc["leader_name"] = gConfig.device_name;

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    JsonObject s = sensors.add<JsonObject>();
    
    s["device_uuid"] = String(item.uuid);   
    s["device_name"] = String(item.client); 
    
    s["temperature_C"] = round(item.temp * 100.0) / 100.0;
    s["battery_voltage"] = (int)(item.volt * 1000);
    s["update_date"] = String(dateBuf);
    s["message"] = "Sensor Status Report (Retry)"; // Précise que c'est un renvoi

    String payload;
    serializeJson(doc, payload);
    return postJsonPayload(payload);
}



bool CloudManager::postJsonPayload(const String& payload) {
    if (_serverUrl.isEmpty()) {
        _lastError = "server_url empty";
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        _lastError = "WiFi not connected";
        return false;
    }

    HTTPClient http;
    if (!http.begin(_serverUrl)) {
        _lastError = "HTTP begin failed";
        return false;
    }

    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(payload);
    if (code < 200 || code >= 300) {
        _lastError = "HTTP POST failed, code=" + String(code);
        http.end();
        return false;
    }

    http.end();
    _lastError = "";
    return true;
}

String CloudManager::getLastError() const {
    return _lastError;
}

void CloudManager::clearError() {
    _lastError = "";
}