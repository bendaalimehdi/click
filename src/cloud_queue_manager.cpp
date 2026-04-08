#include "cloud_queue_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool CloudQueueManager::begin() {
    return true;
}

bool CloudQueueManager::enqueueFromSensorData(const SensorData& data) {
    QueuedCloudItem item;
    item.client = String(data.client);
    item.node = data.id;
    item.temp = data.temp;
    item.volt = data.volt;
    item.createdAt = (uint32_t)time(nullptr);
    item.retryCount = 0;
    return enqueue(item);
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
    auto err = deserializeJson(doc, line);
    if (err) {
        _lastError = "Queue line parse failed";
        return false;
    }

    item.client = String((const char*)(doc["client"] | ""));
    item.node = doc["node"] | 0;
    item.temp = doc["temp"] | NAN;
    item.volt = doc["volt"] | NAN;
    item.createdAt = doc["createdAt"] | 0;
    item.retryCount = doc["retryCount"] | 0;
    return true;
}

String CloudQueueManager::serializeLine(const QueuedCloudItem& item) {
    JsonDocument doc;
    doc["client"] = item.client;
    doc["node"] = item.node;
    doc["temp"] = item.temp;
    doc["volt"] = item.volt;
    doc["createdAt"] = item.createdAt;
    doc["retryCount"] = item.retryCount;

    String out;
    serializeJson(doc, out);
    return out;
}