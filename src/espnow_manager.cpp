#include "espnow_manager.h"

EspNowManager* EspNowManager::_instance = nullptr;

bool EspNowManager::begin() {
    _instance = this;

    if (esp_now_init() != ESP_OK) {
        return false;
    }

    esp_now_register_recv_cb(onDataRecv);
    
    // Correction de la conversion : le type attendu est désormais esp_now_send_cb_t
    esp_now_register_send_cb((esp_now_send_cb_t)onDataSent);

    return true;
}

bool EspNowManager::addPeer(const uint8_t* mac, uint8_t channel, bool encrypt) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = encrypt;

    return esp_now_add_peer(&peerInfo) == ESP_OK;
}

bool EspNowManager::addPeerFromString(const String& macStr, uint8_t channel, bool encrypt) {
    uint8_t mac[6];
    if (!macStringToBytes(macStr, mac)) {
        return false;
    }
    return addPeer(mac, channel, encrypt);
}

bool EspNowManager::sendSensorData(const uint8_t* mac, const SensorData& data) {
    return esp_now_send(mac, reinterpret_cast<const uint8_t*>(&data), sizeof(data)) == ESP_OK;
}

bool EspNowManager::sendSyncData(const uint8_t* mac, const SyncData& data) {
    return esp_now_send(mac, reinterpret_cast<const uint8_t*>(&data), sizeof(data)) == ESP_OK;
}

void EspNowManager::onSensorReceived(SensorCallback cb) {
    _sensorCb = cb;
}

void EspNowManager::onSyncReceived(SyncCallback cb) {
    _syncCb = cb;
}

bool EspNowManager::macStringToBytes(const String& macStr, uint8_t out[6]) {
    int vals[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
               &vals[0], &vals[1], &vals[2],
               &vals[3], &vals[4], &vals[5]) != 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        out[i] = static_cast<uint8_t>(vals[i]);
    }
    return true;
}

String EspNowManager::macBytesToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// Implémentation de la réception compatible ESP32-C6 (v3.0.0+)
void EspNowManager::onDataRecv(const esp_now_recv_info_t* recvInfo, const uint8_t* data, int len) {
    if (!_instance || !recvInfo || !data) {
        return;
    }

    const uint8_t* mac = recvInfo->src_addr;

    if (len == sizeof(SensorData) && _instance->_sensorCb) {
        SensorData packet;
        memcpy(&packet, data, sizeof(packet));
        _instance->_sensorCb(mac, packet);
    } 
    else if (len == sizeof(SyncData) && _instance->_syncCb) {
        SyncData packet;
        memcpy(&packet, data, sizeof(packet));
        _instance->_syncCb(mac, packet);
    }
}

// Implémentation de l'envoi avec la nouvelle signature
void EspNowManager::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    (void)mac;
    (void)status;
}