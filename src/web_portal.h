#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>
#include "config_manager.h"
#include "protocol_types.h"

struct PortalStatus {
    size_t queueSize = 0;
    String lastError;
};

class WebPortal {
public:
    using SaveConfigCallback = std::function<bool(const AppConfig&)>;
    using GetNodesCallback   = std::function<std::vector<NodeRecord>()>;
    using GetStatusCallback  = std::function<PortalStatus()>;

    WebPortal(uint16_t port = 80);

    void begin(const AppConfig& cfg,
               SaveConfigCallback saveCb,
               GetNodesCallback nodesCb,
               GetStatusCallback statusCb);

private:
    AsyncWebServer _server;
    AppConfig _cfg;
    SaveConfigCallback _saveCb;
    GetNodesCallback _nodesCb;
    GetStatusCallback _statusCb;

    String htmlPage();
    void setupRoutes();
};