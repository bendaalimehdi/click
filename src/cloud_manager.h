#pragma once

#include <Arduino.h>
#include "protocol_types.h"
#include "cloud_queue_manager.h"
#include "time_manager.h"

class CloudManager {
public:
    explicit CloudManager(const String& serverUrl, TimeManager& timeMgr);

    bool postSensorData(const SensorData& data, const uint8_t* sensorMac, 
                        const String& leaderMac, const String& leaderName);
    bool postQueuedItem(const QueuedCloudItem& item);

    String getLastError() const;
    void clearError();

private:
    String _serverUrl;
    String _lastError;
    TimeManager& _time;

    bool postJsonPayload(const String& payload);
};