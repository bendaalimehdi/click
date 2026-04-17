#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include "config_manager.h"
#include "protocol_types.h"
#include "espnow_manager.h"
#include "cloud_manager.h"
#include "time_manager.h"
#include "web_portal.h"
#include "cloud_queue_manager.h"
#include "leader_state_manager.h"
#include "error_tracker.h"
#include <led_manager.h>

class LeaderService {
public:
    explicit LeaderService(const AppConfig& cfg);

    bool begin();
    void loop();

    bool provisionNode(const String& mac, const String& name);

    // Getters pour le portail Web
    std::vector<NodeRecord> getNodes() const;
    PortalStatus getPortalStatus() const;
    
    // Actions de configuration
    bool saveConfig(const AppConfig& cfg);
    void maintainWiFi();


   

    


private:
    AppConfig _cfg;
    EspNowManager _espnow;
    TimeManager _time;
    CloudManager _cloud;
    WebPortal _portal;
    CloudQueueManager _queue;
    LeaderStateManager _state;
    ErrorTracker _errors;

    using ProvisionCallback = std::function<bool(const String&, const String&)>;

    // Gestion des données en mémoire
    std::vector<NodeRecord> _nodes;
    bool _nodesDirty = false; // Flag pour limiter les écritures LittleFS

    // Liste temporaire pour le provisioning (MAC -> Infos)
    std::map<String, DiscoveredFollower> _discoveredList;

    // Timers pour les tâches de fond
    uint32_t _lastWiFiRetryMs = 0;
    uint32_t _lastQueueRetryMs = 0;
    uint32_t _lastStateSaveMs = 0;
    uint32_t _lastDiscoveryCleanupMs = 0;

    // Initialisation
    void setupWiFi();
    void setupApPortal();
    uint8_t scanForLeaderChannel(const String& apSsid);
    LedManager _led;

    // Gestionnaires de paquets
    void handleSensorPacket(const uint8_t* mac, const SensorData& packet);
    void handlePairingRequest(const uint8_t* mac, PairingHello* msg);
    
    // Logique interne
    NodeRecord* findNodeById(int id);
    void retryQueuedCloudPosts();
    void persistLeaderStateIfNeeded();
    void cleanupDiscovery();

};