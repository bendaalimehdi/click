#pragma once

#include <Arduino.h>
#include <vector>
#include "protocol_types.h"
#include "time_manager.h"



class CloudQueueManager {
public:
    CloudQueueManager(TimeManager& timeMgr);
    bool begin();

    
    void enqueueFromSensorData(const SensorData& packet, const uint8_t* mac);
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
    TimeManager& _time;

    bool parseLine(const String& line, QueuedCloudItem& item);
    String serializeLine(const QueuedCloudItem& item);
};
