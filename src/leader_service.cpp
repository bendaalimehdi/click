#include "leader_service.h"
#include "config_manager.h"
#include "logger.h"
#include <WiFi.h>

LeaderService::LeaderService(const AppConfig& cfg)
    : _cfg(cfg), _cloud(cfg.server_url, _time), _portal(80) {}

bool LeaderService::begin() {
    setupWiFi();

    if (!_queue.begin()) {
        Logger::error("Queue init failed: " + _queue.getLastError());
        _errors.setLastError(_queue.getLastError());
    }

    if (!_state.begin()) {
        Logger::error("State init failed: " + _state.getLastError());
        _errors.setLastError(_state.getLastError());
    }

    if (_state.loadNodes(_nodes)) {
        Logger::info("Leader state restored: " + String(_nodes.size()) + " nodes");
    } else {
        Logger::warn("Leader state restore failed: " + _state.getLastError());
        _errors.setLastError(_state.getLastError());
    }

    _time.begin(_cfg.timezone);
    if (!_time.isTimeSynced(15000)) {
        Logger::warn("NTP sync not ready, fallback timing may apply");
    }

    if (!_espnow.begin()) {
        Logger::error("Leader: ESP-NOW init failed");
        _errors.setLastError("ESP-NOW init failed");
        return false;
    }

    _espnow.onSensorReceived([this](const uint8_t* mac, const SensorData& packet) {
        handleSensorPacket(mac, packet);
    });

    setupApPortal();

    Logger::info("Leader ready");
    Logger::info("Time now: " + _time.nowString());
    return true;
}

void LeaderService::maintainWiFi() {
    const uint32_t retryIntervalMs = 15000;
    if (_cfg.wifi_ssid.isEmpty() || WiFi.status() == WL_CONNECTED) return;
    if (millis() - _lastWiFiRetryMs < retryIntervalMs) return;

    _lastWiFiRetryMs = millis();
    Logger::warn("WiFi disconnected, reconnecting...");
    WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
}

void LeaderService::setupWiFi() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAP(_cfg.ap_ssid.c_str(), _cfg.ap_pass.c_str())) {
        Logger::info("AP IP: " + WiFi.softAPIP().toString());
    }
    if (!_cfg.wifi_ssid.isEmpty()) {
        WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
    }
}

void LeaderService::setupApPortal() {
    _portal.begin(
        _cfg,
        [this](const AppConfig& newCfg) -> bool { return saveConfig(newCfg); },
        [this]() -> std::vector<NodeRecord> { return getNodes(); },
        [this]() -> PortalStatus { return getPortalStatus(); }
    );
}

void LeaderService::loop() {
    maintainWiFi();
    
    static uint32_t lastCleanup = 0;
    if (millis() - lastCleanup > 60000) { // Nettoyage toutes les minutes
        lastCleanup = millis();
        for (auto it = _nodes.begin(); it != _nodes.end();) {
            // Suppression des nœuds non vus depuis 24h
            if (millis() - it->lastSeenMs > 24UL * 3600UL * 1000UL) {
                it = _nodes.erase(it);
                _nodesDirty = true; 
            } else {
                ++it;
            }
        }
    }

    retryQueuedCloudPosts();
    persistLeaderStateIfNeeded();
}

void LeaderService::handleSensorPacket(const uint8_t* mac, const SensorData& packet) {
    String macStr = EspNowManager::macBytesToString(mac);
    Logger::info("RX " + macStr + " id=" + String(packet.id) + " temp=" + String(packet.temp, 2));

    NodeRecord* node = findNodeById(packet.id);
    if (!node) {
        NodeRecord newNode;
        newNode.client = packet.client;
        newNode.id = packet.id;
        newNode.temp = packet.temp;
        newNode.volt = packet.volt;
        newNode.lastSeenMs = millis();
        newNode.mac = macStr;
        _nodes.push_back(newNode);
        _nodesDirty = true;
    } else {
        node->client = packet.client;
        node->temp = packet.temp;
        node->volt = packet.volt;
        node->lastSeenMs = millis();
        node->mac = macStr;
        _nodesDirty = true;
    }

    // Réponse de synchronisation
    SyncData sync;
    sync.next_sleep_seconds = _time.secondsUntilNextSlot(_cfg.report_times);
    _espnow.addPeer(mac);
    _espnow.sendSyncData(mac, sync);

    // Envoi Cloud ou mise en file d'attente
    if (!_cloud.postSensorData(packet, mac, _cfg.leader_mac, _cfg.device_name)) {
        Logger::warn("Cloud POST failed, queuing data");
        _errors.setLastError(_cloud.getLastError());
        _queue.enqueueFromSensorData(packet);
    } else {
        _errors.clear();
    }
}

void LeaderService::persistLeaderStateIfNeeded() {
    const uint32_t saveIntervalMs = 60000; // Max une écriture par minute
    if (!_nodesDirty || (millis() - _lastStateSaveMs < saveIntervalMs)) return;

    _lastStateSaveMs = millis();
    if (_state.saveNodes(_nodes)) {
        Logger::info("Nodes persisted to LittleFS");
        _nodesDirty = false;
    } else {
        _errors.setLastError(_state.getLastError());
    }
}

void LeaderService::retryQueuedCloudPosts() {
    const uint32_t retryIntervalMs = 30000;
    if (millis() - _lastQueueRetryMs < retryIntervalMs) return;
    _lastQueueRetryMs = millis();

    if (WiFi.status() != WL_CONNECTED) return;

    std::vector<QueuedCloudItem> items;
    if (!_queue.loadAll(items) || items.empty()) return;

    Logger::info("Retrying " + String(items.size()) + " queued items");
    bool changed = false;

    while (!items.empty()) {
        if (_cloud.postQueuedItem(items.front())) {
            items.erase(items.begin());
            changed = true;
        } else {
            break; // Arrêt si le serveur est toujours indisponible
        }
    }

    if (changed) {
        _queue.rewriteAll(items);
    }
}

NodeRecord* LeaderService::findNodeById(int id) {
    for (auto& n : _nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

std::vector<NodeRecord> LeaderService::getNodes() const { return _nodes; }

bool LeaderService::saveConfig(const AppConfig& cfg) {
    ConfigManager cm;
    if (!cm.begin()) return false;
    return cm.save(cfg);
}

PortalStatus LeaderService::getPortalStatus() const {
    PortalStatus st;
    st.queueSize = const_cast<CloudQueueManager&>(_queue).size();
    st.lastError = _errors.getLastError();
    return st;
}