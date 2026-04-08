#pragma once

#include <Arduino.h>
#include <vector>
#include "config_manager.h"
#include "protocol_types.h"
#include "espnow_manager.h"
#include "cloud_manager.h"
#include "time_manager.h"
#include "web_portal.h"
#include "cloud_queue_manager.h"
#include "leader_state_manager.h"
#include "error_tracker.h"

class LeaderService {
public:
    explicit LeaderService(const AppConfig& cfg);

    bool begin();
    void loop();
    std::vector<NodeRecord> getNodes() const;
    bool saveConfig(const AppConfig& cfg);
    PortalStatus getPortalStatus() const;
    uint32_t _lastWiFiRetryMs = 0;
    void maintainWiFi();

private:
    AppConfig _cfg;
    EspNowManager _espnow;
    CloudManager _cloud;
    TimeManager _time;
    WebPortal _portal;
    CloudQueueManager _queue;
    LeaderStateManager _state;
    ErrorTracker _errors;

    std::vector<NodeRecord> _nodes;

    uint32_t _lastQueueRetryMs = 0;
    uint32_t _lastStateSaveMs = 0;

    void setupWiFi();
    void setupApPortal();
    void handleSensorPacket(const uint8_t* mac, const SensorData& packet);
    NodeRecord* findNodeById(int id);

    void retryQueuedCloudPosts();
    void persistLeaderStateIfNeeded();
};