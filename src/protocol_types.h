#pragma once
#include <Arduino.h>

#pragma pack(push, 1)


// Types de messages pour distinguer les paquets ESP-NOW
enum MsgType : uint8_t {
    MSG_SENSOR_DATA = 0x01,
    MSG_SYNC_DATA   = 0x02,
    MSG_PAIRING_REQ = 0x10, // PairingHello
    MSG_PROVISION   = 0x20, // ProvisionConfig
    MSG_PROV_ACK    = 0x12, // ProvisionAck
    MSG_IDENTIFY    = 0x13  // Commande pour faire clignoter la LED
};

struct ProvisionConfigMsg {
    MsgType type = (MsgType)MSG_PROVISION;
    char leader_mac[18];   // La MAC du leader pour que le follower sache qui écouter
    char device_name[32];  // Le nom que vous avez saisi dans l'interface
    uint8_t channel;       // Le canal WiFi utilisé
};

#pragma pack(pop)

// Message d'appairage envoyé par le Follower en mode INSTALL
struct PairingHello {
    MsgType type = MSG_PAIRING_REQ;
    char uid[18];          // Adresse MAC servant d'ID unique
    char firmware[8];      // Version du firmware
    char sensor_type[12];  // Type de capteur (ex: bmp280)
    float battery_voltage;
};



// Mise à jour de la structure SensorData avec le type de message
struct SensorData {
    MsgType type = MSG_SENSOR_DATA;
    char client[16];
    int id;
    float temp;
    float volt;
    int rssi; 
    uint32_t count; 
};

struct SyncData {
    MsgType type = MSG_SYNC_DATA;
    uint32_t next_sleep_seconds;
};



// Structure pour stocker les followers détectés côté Leader (RAM)
struct DiscoveredFollower {
    String mac;
    String firmware;
    String sensorType;
    float volt;
    uint32_t lastSeenMs;
};

// src/protocol_types.h
struct NodeRecord {
    String client;
    int id = 0;
    float temp = NAN;
    float volt = NAN;
    uint32_t lastSeenMs = 0;
    String mac;
}; 