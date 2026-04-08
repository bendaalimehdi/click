#include "cloud_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "espnow_manager.h"

CloudManager::CloudManager(const String& serverUrl, TimeManager& timeMgr)
    : _serverUrl(serverUrl), _time(timeMgr) {} //

bool CloudManager::postSensorData(const SensorData& data, const uint8_t* sensorMac, 
                                 const String& leaderMac, const String& leaderName) {
    // Formatage UUID Capteur (MAC sans ":")
    String sMacStr = EspNowManager::macBytesToString(sensorMac);
    String sensorUuid = "";
    for (char c : sMacStr) { if (c != ':') sensorUuid += c; }

    // Formatage UUID Leader (MAC sans ":")
    String lUuid = "";
    for (char c : leaderMac) { if (c != ':') lUuid += c; }

    JsonDocument doc;
    doc["statusCode"] = 200;
    doc["update_date"] = _time.nowString();
    
    // Nouveaux champs demandés
    doc["device_uuid"] = sensorUuid;      // UUID du capteur
    doc["leader_uuid"] = lUuid;           // UUID du leader
    doc["leader_name"] = leaderName;      // Nom du leader (ex: "leader-1")

    // Autres champs
    doc["temperature_C"] = String(data.temp, 2);
    doc["battery_voltage"] = (int)(data.volt * 1000);
    doc["user_identify"] = "Bind_Via_Gateway";
    // ... (autres champs habituels)

    String payload;
    serializeJson(doc, payload);
    return postJsonPayload(payload);
}

bool CloudManager::postQueuedItem(const QueuedCloudItem& item) {
    JsonDocument doc;
    doc["client"] = item.client;
    doc["node"] = item.node;
    doc["temp"] = item.temp;
    doc["volt"] = item.volt;

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