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
    // Definition des callbacks pour l'interaction avec LeaderService
    using SaveConfigCallback = std::function<bool(const AppConfig&)>;
    using GetNodesCallback   = std::function<std::vector<NodeRecord>()>;
    using GetStatusCallback  = std::function<PortalStatus()>;

    using GetDiscoveredCallback = std::function<std::vector<DiscoveredFollower>()>;
    using ProvisionCallback = std::function<bool(const String&, const String&)>;
    

    WebPortal(uint16_t port = 80);

    // Mise à jour de la méthode begin pour inclure le callback de découverte
    void begin(const AppConfig& cfg,
               SaveConfigCallback saveCb,
               GetNodesCallback nodesCb,
               GetStatusCallback statusCb,
               GetDiscoveredCallback discoveredCb,
               ProvisionCallback provisionCb);

private:
    AsyncWebServer _server;
    AppConfig _cfg;
    SaveConfigCallback _saveCb;
    GetNodesCallback _nodesCb;
    GetStatusCallback _statusCb;
    GetDiscoveredCallback _discoveredCb; 
    ProvisionCallback _provisionCb;

    String htmlPage();
    void setupRoutes();
};