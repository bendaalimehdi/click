#pragma once

#include <Arduino.h>
#include <vector>
#include "protocol_types.h"

struct QueuedCloudItem {
    String client;
    int node = 0;
    float temp = NAN;
    float volt = NAN;
    uint32_t createdAt = 0;
    uint32_t retryCount = 0;
};

class CloudQueueManager {
public:
    bool begin();

    bool enqueueFromSensorData(const SensorData& data);
    bool enqueue(const QueuedCloudItem& item);

    bool peek(QueuedCloudItem& item);
    bool pop();
    size_t size();
    bool isEmpty();

    bool loadAll(std::vector<QueuedCloudItem>& out);
    bool rewriteAll(const std::vector<QueuedCloudItem>& items);

    String getLastError() const;

private:
    String _lastError;
    const char* _queueFile = "/cloud_queue.ndjson";

    bool parseLine(const String& line, QueuedCloudItem& item);
    String serializeLine(const QueuedCloudItem& item);
};
