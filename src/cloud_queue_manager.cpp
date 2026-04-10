#include "cloud_queue_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "espnow_manager.h"

CloudQueueManager::CloudQueueManager(TimeManager& timeMgr) : _time(timeMgr) {
    // Initialisation simple
}

bool CloudQueueManager::begin() {
    return true;
}

void CloudQueueManager::enqueueFromSensorData(const SensorData& packet, const uint8_t* mac) {
    QueuedCloudItem item;
    
    // Correction erreur 413 : On utilise strlcpy
    strlcpy(item.client, packet.client, sizeof(item.client));

    String macStr = EspNowManager::macBytesToString(mac);
    String cleanUuid = "";
    for (char c : macStr) { 
        if (c != ':') cleanUuid += c; 
    }
    
    // Correction membre "uuid" : maintenant reconnu grâce à protocol_types.h
    strlcpy(item.uuid, cleanUuid.c_str(), sizeof(item.uuid));

    item.temp = packet.temp;
    item.volt = packet.volt;
    
    // Correction erreur getNow : vérifiez le nom exact dans time_manager.h (souvent getEpoch)
    item.createdAt = _time.getNow();

    enqueue(item); 
}

bool CloudQueueManager::enqueue(const QueuedCloudItem& item) {
    File f = LittleFS.open(_queueFile, "a");
    if (!f) {
        _lastError = "Failed to open queue file for append";
        return false;
    }

    String line = serializeLine(item);
    if (!f.println(line)) {
        f.close();
        _lastError = "Failed to append queue line";
        return false;
    }

    f.close();
    return true;
}

bool CloudQueueManager::peek(QueuedCloudItem& item) {
    File f = LittleFS.open(_queueFile, "r");
    if (!f) {
        _lastError = "Queue file open failed for read";
        return false;
    }

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        f.close();
        return parseLine(line, item);
    }

    f.close();
    _lastError = "Queue is empty";
    return false;
}

bool CloudQueueManager::pop() {
    std::vector<QueuedCloudItem> items;
    if (!loadAll(items)) return false;
    if (items.empty()) return true;

    items.erase(items.begin());
    return rewriteAll(items);
}

size_t CloudQueueManager::size() {
    std::vector<QueuedCloudItem> items;
    if (!loadAll(items)) return 0;
    return items.size();
}

bool CloudQueueManager::isEmpty() {
    return size() == 0;
}

bool CloudQueueManager::loadAll(std::vector<QueuedCloudItem>& out) {
    out.clear();

    File f = LittleFS.open(_queueFile, "r");
    if (!f) {
        File nf = LittleFS.open(_queueFile, "w");
        if (nf) nf.close();
        return true;
    }

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;

        QueuedCloudItem item;
        if (parseLine(line, item)) {
            out.push_back(item);
        }
    }

    f.close();
    return true;
}

bool CloudQueueManager::rewriteAll(const std::vector<QueuedCloudItem>& items) {
    File f = LittleFS.open(_queueFile, "w");
    if (!f) {
        _lastError = "Failed to rewrite queue file";
        return false;
    }

    for (const auto& item : items) {
        if (!f.println(serializeLine(item))) {
            f.close();
            _lastError = "Failed during queue rewrite";
            return false;
        }
    }

    f.close();
    return true;
}

String CloudQueueManager::getLastError() const {
    return _lastError;
}

bool CloudQueueManager::parseLine(const String& line, QueuedCloudItem& item) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return false;

    strlcpy(item.client, doc["client"] | "", sizeof(item.client));
    strlcpy(item.uuid, doc["uuid"] | "", sizeof(item.uuid)); // Lecture de l'UUID
    item.temp = doc["temp"] | 0.0f;
    item.volt = doc["volt"] | 0.0f;
    item.createdAt = doc["createdAt"] | 0;
    return true;
}
String CloudQueueManager::serializeLine(const QueuedCloudItem& item) {
    JsonDocument doc;
    doc["client"] = String(item.client);
    doc["uuid"] = String(item.uuid); // Ajout de l'UUID dans le JSON local
    doc["temp"] = item.temp;
    doc["volt"] = item.volt;
    doc["createdAt"] = item.createdAt;

    String out;
    serializeJson(doc, out);
    return out;
}