#include "leader_service.h"
#include "config_manager.h"
#include "protocol_types.h"
#include "logger.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>



LeaderService::LeaderService(const AppConfig& cfg)
    : _cfg(cfg),
      _cloud(cfg.server_url, _time),
      _queue(_time),
      _portal(80),
      _led() {}

bool LeaderService::begin() {
    _led.begin();
    _led.setRed();
    setupWiFi();

     uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        _led.setGreen();
    }

     // Sauvegarder le canal effectif dans la config
    uint8_t channel = 1;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&channel, &second);
    
    if (channel != _cfg.wifi_channel) {
        Logger::info("Channel updated: " + String(channel));
        _cfg.wifi_channel = channel;
        saveConfig(_cfg); // persisté immédiatement
    }

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

    // Handler mis à jour pour traiter différents types de paquets
 _espnow.onSensorReceived([this](const uint8_t* mac, const SensorData& packet) {
        // 1. Accès sécurisé au premier octet pour identifier le type de message
        // On utilise un pointeur uint8_t pour lire l'en-tête sans risque de corruption
        const uint8_t* rawData = (const uint8_t*)&packet;
        uint8_t msgType = rawData[0]; 

        // 2. Traitement selon le type de message
        if (msgType == MSG_PAIRING_REQ) {
            // Cast sécurisé vers PairingHello pour lire les infos d'installation
            PairingHello* msg = (PairingHello*)rawData;
            String macStr = EspNowManager::macBytesToString(mac);

            // Création ou mise à jour du follower détecté dans la map RAM
            DiscoveredFollower f;
            f.mac = macStr;
            f.volt = msg->battery_voltage;
            f.sensorType = String(msg->sensor_type);
            f.lastSeenMs = millis();
            
            _discoveredList[macStr] = f; 
            
            // LOG CRITIQUE pour le débug : Si vous voyez ça, la radio fonctionne !
            Logger::info("Detected new follower: " + macStr + " (" + f.sensorType + ")");
        } 
        else if (msgType == MSG_SENSOR_DATA) {
            // Mode normal : On traite les données de température
            handleSensorPacket(mac, packet);
        }
        else {
            // Log de sécurité pour voir si d'autres paquets arrivent
            Logger::warn("Unknown msg type received: " + String(msgType));
        }
    });

    setupApPortal();

    Logger::info("Leader ready");
    Logger::info("Time now: " + _time.nowString());
    return true;
}




bool LeaderService::provisionNode(const String& targetMacStr, const String& nodeName) {
    uint8_t targetMac[6];
    if (!EspNowManager::macStringToBytes(targetMacStr, targetMac)) {
        Logger::error("Provision failed: invalid target MAC " + targetMacStr);
        return false;
    }

     // Lire le canal réel actif
    uint8_t channel = 1;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&channel, &second);
    Logger::info("Provisioning on channel: " + String(channel));

    JsonDocument doc;
    doc["type"] = "provision";
    doc["leader_mac"] = WiFi.macAddress();
    doc["device_name"] = nodeName;
    doc["role"] = "follower";
    doc["channel"] = channel;

    String payload;
    serializeJson(doc, payload);

    _espnow.addPeer(targetMac, channel, false);

    bool ok = _espnow.sendRaw(
        targetMac,
        reinterpret_cast<const uint8_t*>(payload.c_str()),
        payload.length() + 1
    );

    if (ok) {
        Logger::info("Config envoyée à " + targetMacStr + " (Nom: " + nodeName + ")");
        Logger::info("Provision JSON: " + payload);
        _discoveredList.erase(targetMacStr);
    } else {
        Logger::error("Echec envoi provisioning à " + targetMacStr);
    }

    return ok;
}

void LeaderService::maintainWiFi() {
    const uint32_t retryIntervalMs = 15000;
    if (_cfg.wifi_ssid.isEmpty()) return;

    if (WiFi.status() == WL_CONNECTED) {
        // S'assurer que la LED est verte si on était déconnecté
        return;
    }

    _led.setRed(); // Perdu le WiFi → rouge
    if (millis() - _lastWiFiRetryMs < retryIntervalMs) return;
    _lastWiFiRetryMs = millis();

    Logger::warn("WiFi disconnected, reconnecting...");
    WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());

    // Vérifier après tentative
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
        _led.setGreen();
    }
}

void LeaderService::setupWiFi() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    
    // Fixer le canal WiFi est crucial pour que ESP-NOW fonctionne avec les followers
    if (WiFi.softAP(_cfg.ap_ssid.c_str(), _cfg.ap_pass.c_str(), 1)) {
        Logger::info("AP IP: " + WiFi.softAPIP().toString());
    }

    if (!_cfg.wifi_ssid.isEmpty()) {
        WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
    }
}

void LeaderService::setupApPortal() {
    _portal.begin(
        _cfg,
        [this](const AppConfig& c) { return saveConfig(c); },
        [this]() { return getNodes(); },
        [this]() { return getPortalStatus(); },
        [this]() { 
            std::vector<DiscoveredFollower> list;
            for(auto const& [mac, f] : _discoveredList) list.push_back(f);
            return list;
        },
        // AJOUTE BIEN CE DERNIER BLOC :
        [this](const String& mac, const String& name) { 
            return provisionNode(mac, name); 
        }
    );
}

void LeaderService::loop() {
     _led.tick();
    maintainWiFi();
    
    // Nettoyage des nœuds expirés (24h)
    static uint32_t lastCleanup = 0;
    if (millis() - lastCleanup > 60000) { 
        lastCleanup = millis();
        for (auto it = _nodes.begin(); it != _nodes.end();) {
            if (millis() - it->lastSeenMs > 24UL * 3600UL * 1000UL) {
                it = _nodes.erase(it);
                _nodesDirty = true; // Marqué pour sauvegarde
            } else {
                ++it;
            }
        }
    }

    retryQueuedCloudPosts();
    persistLeaderStateIfNeeded();
}

void LeaderService::handleSensorPacket(const uint8_t* mac, const SensorData& packet) {
    _led.blinkBlue(4);
    String macStr = EspNowManager::macBytesToString(mac);
    Logger::info("Data from " + macStr + " id=" + String(packet.id) + " temp=" + String(packet.temp, 2));

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

    // Lire le canal réel actif avant de répondre
    uint8_t channel = 1;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&channel, &second);
    Logger::info("Sending SyncData on channel: " + String(channel));

    // Ajouter/mettre à jour le peer sur le bon canal
    _espnow.addPeer(mac, channel, false);

    // Réponse de synchronisation pour le sommeil
    SyncData sync;
    sync.type = MSG_SYNC_DATA;
    sync.next_sleep_seconds = _time.secondsUntilNextSlot(_cfg.report_times);
    
    _espnow.sendSyncData(mac, sync);

    // Envoi au Cloud
    if (!_cloud.postSensorData(packet, mac, _cfg.leader_mac, _cfg.device_name)) {
        Logger::warn("Cloud failed, data queued");
        _errors.setLastError(_cloud.getLastError());
        
        // CORRECTION ICI : Passez l'argument 'mac'
        _queue.enqueueFromSensorData(packet, mac); 
    } else {
        _errors.clear();
    }
}

void LeaderService::handlePairingRequest(const uint8_t* mac, PairingHello* msg) {
    String macStr = EspNowManager::macBytesToString(mac);
    Logger::info("Provisioning request from: " + macStr);
    
    // Ici, vous stockeriez l'info dans une map _discoveredList 
    // pour l'afficher sur le portail Web (Installation Mode)
}

void LeaderService::persistLeaderStateIfNeeded() {
    const uint32_t saveIntervalMs = 60000; 
    if (!_nodesDirty || (millis() - _lastStateSaveMs < saveIntervalMs)) return;

    _lastStateSaveMs = millis();
    if (_state.saveNodes(_nodes)) {
        Logger::info("State auto-saved to LittleFS");
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

    Logger::info("Retry queue: " + String(items.size()) + " items");
    bool changed = false;

    // Utilisation d'un itérateur sécurisé
    for (auto it = items.begin(); it != items.end(); ) {
        if (_cloud.postQueuedItem(*it)) {
            it = items.erase(it);
            changed = true;
        } else {
            break; // Serveur toujours HS, on arrête pour ce cycle
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