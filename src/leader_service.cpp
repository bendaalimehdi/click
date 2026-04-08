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
    const uint32_t retryIntervalMs = 10000;

    if (_cfg.wifi_ssid.isEmpty()) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (millis() - _lastWiFiRetryMs < retryIntervalMs) {
        return;
    }

    _lastWiFiRetryMs = millis();

    Logger::warn("WiFi disconnected, reconnecting STA...");
    WiFi.disconnect(false, false);
    WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
}

void LeaderService::setupWiFi() {
    WiFi.persistent(false); // Prevent flash wear and potential config corruption
    WiFi.mode(WIFI_AP_STA);

    if (WiFi.softAP(_cfg.ap_ssid.c_str(), _cfg.ap_pass.c_str())) {
        Logger::info("AP IP: " + WiFi.softAPIP().toString());
    }

    if (!_cfg.wifi_ssid.isEmpty()) {
        WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
        // Increase timeout to 20 seconds for slower routers
    }
}
void LeaderService::setupApPortal() {
    _portal.begin(
        _cfg,
        [this](const AppConfig& newCfg) -> bool {
            return saveConfig(newCfg);
        },
        [this]() -> std::vector<NodeRecord> {
            return getNodes();
        },
        [this]() -> PortalStatus {
            return getPortalStatus();
        }
    );
}

void LeaderService::loop() {
    maintainWiFi();
    static uint32_t lastCleanup = 0;
    

    if (millis() - lastCleanup > 30000) {
        lastCleanup = millis();

        for (auto it = _nodes.begin(); it != _nodes.end();) {
            if (millis() - it->lastSeenMs > 24UL * 3600UL * 1000UL) {
                it = _nodes.erase(it);
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

    Logger::info(
        "RX " + macStr +
        " client=" + String(packet.client) +
        " id=" + String(packet.id) +
        " temp=" + String(packet.temp, 2) +
        " volt=" + String(packet.volt, 2)
    );

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
    } else {
        node->client = packet.client;
        node->temp = packet.temp;
        node->volt = packet.volt;
        node->lastSeenMs = millis();
        node->mac = macStr;
    }

    SyncData sync;
    sync.next_sleep_seconds = _time.secondsUntilNextSlot(_cfg.report_times);

    _espnow.addPeer(mac);
    if (!_espnow.sendSyncData(mac, sync)) {
        Logger::warn("Failed to send SyncData to " + macStr);
        _errors.setLastError("Failed to send SyncData");
    }

    bool posted = _cloud.postSensorData(packet, mac, _cfg.leader_mac, _cfg.device_name);
    if (!posted) {
        Logger::warn("Cloud POST failed: " + _cloud.getLastError());
        _errors.setLastError(_cloud.getLastError());

        if (_queue.enqueueFromSensorData(packet)) {
            Logger::info("Payload queued to LittleFS");
        } else {
            Logger::error("Queue append failed: " + _queue.getLastError());
            _errors.setLastError(_queue.getLastError());
        }
    } else {
        _errors.clear();
    }

    if (!_state.saveNodes(_nodes)) {
        Logger::warn("Immediate state save failed: " + _state.getLastError());
        _errors.setLastError(_state.getLastError());
    }
}

NodeRecord* LeaderService::findNodeById(int id) {
    for (auto& n : _nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

std::vector<NodeRecord> LeaderService::getNodes() const {
    return _nodes;
}

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

void LeaderService::retryQueuedCloudPosts() {
    const uint32_t retryIntervalMs = 15000;
    if (millis() - _lastQueueRetryMs < retryIntervalMs) return;
    _lastQueueRetryMs = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Logger::warn("Retry skipped: WiFi not connected");
        return;
    }

    std::vector<QueuedCloudItem> items;
    if (!_queue.loadAll(items)) {
        Logger::warn("Queue load failed: " + _queue.getLastError());
        _errors.setLastError(_queue.getLastError());
        return;
    }

    if (items.empty()) return;

    Logger::info("Retry queue start, items=" + String(items.size()));

    bool changed = false;
    size_t sentCount = 0;

    while (!items.empty()) {
        QueuedCloudItem& item = items.front();

        if (_cloud.postQueuedItem(item)) {
            Logger::info("Queued payload sent for node " + String(item.node));
            items.erase(items.begin());
            changed = true;
            sentCount++;
        } else {
            item.retryCount++;
            Logger::warn(
                "Queued payload failed for node " + String(item.node) +
                ", retryCount=" + String(item.retryCount) +
                ", err=" + _cloud.getLastError()
            );
            _errors.setLastError(_cloud.getLastError());
            changed = true;
            break;
        }
    }

    if (changed) {
        if (!_queue.rewriteAll(items)) {
            Logger::error("Queue rewrite failed: " + _queue.getLastError());
            _errors.setLastError(_queue.getLastError());
        }
    }

    if (sentCount > 0 && items.empty()) {
        Logger::info("Queue fully flushed");
        _errors.clear();
    }
}

void LeaderService::persistLeaderStateIfNeeded() {
    const uint32_t saveIntervalMs = 60000;
    if (millis() - _lastStateSaveMs < saveIntervalMs) return;
    _lastStateSaveMs = millis();

    if (!_state.saveNodes(_nodes)) {
        Logger::warn("Periodic state save failed: " + _state.getLastError());
        _errors.setLastError(_state.getLastError());
    }
}