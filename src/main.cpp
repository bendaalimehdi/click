#include <Arduino.h>
#include "config_manager.h"
#include "leader_service.h"
#include "follower_service.h"
#include "logger.h"

// --- Constants ---
const int BOOT_BUTTON_PIN = 9; // Physical BOOT button on Super Mini C6

// --- Global Declarations ---
ConfigManager gConfigManager;
AppConfig gConfig;
LeaderService* gLeader = nullptr;
FollowerService* gFollower = nullptr;

void setup() {
    // 1. Initialisation de la console série
    Serial.begin(115200);
    
    // 2. SAFE BOOT CHECK (GPIO 9)
    // We do this immediately before any other services start
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    delay(200); // Debounce delay

    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        Logger::info("!!! SAFE MODE DETECTED !!!");
        Logger::info("Holding system awake for maintenance/upload.");
        
        // Visual indicator: If you have an LED, blink it here
        // Using GP15 (common on Mini) as a fallback blinker
        pinMode(15, OUTPUT); 

        while (true) {
            digitalWrite(15, !digitalRead(15)); // Blink
            Serial.print(".");
            delay(500);
            
            // This loop prevents the code from reaching the deep sleep logic below
        }
    }

    // 3. Normal Boot Process
    // Safety delay to prevent accidental Deep Sleep loops
    delay(3000); 

    Logger::info("=== FRIDGE MONITOR BOOT ===");

    // Initialisation du système de fichiers LittleFS
    if (!gConfigManager.begin()) {
        Logger::error("LittleFS init failed");
        return;
    }

    // Chargement de la configuration depuis config.json
    if (!gConfigManager.load(gConfig)) {
        Logger::error("Config load failed: " + gConfigManager.getLastError());
        return;
    }

    Logger::info("Role identifie: " + gConfig.role);

    // Lancement du service selon le rôle
    if (gConfig.role == "leader") {
        gLeader = new LeaderService(gConfig);
        if (!gLeader->begin()) {
            Logger::error("Leader init failed");
        }
    } else if (gConfig.role == "follower") {
        gFollower = new FollowerService(gConfig);
        // This will trigger ESP.deepSleep() if configured
        gFollower->beginAndSleep();
    } else {
        Logger::error("Role invalide dans config.json");
    }
}

void loop() {
    // Only used by Leader mode
    if (gLeader) {
        gLeader->loop();
    }
    
    // Yield to RTOS
    delay(10); 
}