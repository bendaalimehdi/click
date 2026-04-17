#include "config_manager.h"
#include <LittleFS.h>

static const char* CONFIG_FILE = "/config.json";

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        _lastError = "LittleFS mount failed";
        return false;
    }
    return true;
}

bool ConfigManager::load(AppConfig& cfg) {
    File f = LittleFS.open(CONFIG_FILE, "r");
    
    if (!f) {
        Serial.println("[CONFIG] config.json manquant. Création des valeurs de sécurité...");
        // Valeurs par défaut sécurisées pour ESP32-S3
        cfg.gpio_pins.battery_adc = 13; // Pin A12 valide
        cfg.role = "follower";
        cfg.leader_mac = "58:8C:81:A9:13:68";
        return save(cfg); // Crée le fichier physiquement
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) return false;
    return applyJsonToConfig(doc, cfg);
}

bool ConfigManager::save(const AppConfig& cfg) {
    JsonDocument doc;
    configToJson(cfg, doc);

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) {
        _lastError = "Cannot open config.json for write";
        return false;
    }

    if (serializeJsonPretty(doc, f) == 0) {
        f.close();
        _lastError = "Failed to write config.json";
        return false;
    }

    f.close();
    return true;
}

bool ConfigManager::saveFromJson(const JsonDocument& doc) {
    AppConfig cfg;
    if (!applyJsonToConfig(doc, cfg)) return false;
    return save(cfg);
}

String ConfigManager::getLastError() const {
    return _lastError;
}

bool ConfigManager::applyJsonToConfig(const JsonDocument& doc, AppConfig& cfg) {
    Serial.printf("DEBUG: PIN ADC lu dans JSON = %d\n", (int)doc["gpio_pins"]["battery_adc"]);
    cfg.role = doc["role"] | cfg.role;
    cfg.id = doc["id"] | cfg.id;
    cfg.device_name = String((const char*)(doc["device_name"] | cfg.device_name.c_str()));
    cfg.wifi_ssid = String((const char*)(doc["wifi_ssid"] | ""));
    cfg.wifi_pass = String((const char*)(doc["wifi_pass"] | ""));
    cfg.wifi_channel = doc["wifi_channel"] | cfg.wifi_channel;
    cfg.server_url = String((const char*)(doc["server_url"] | ""));
    cfg.leader_mac = String((const char*)(doc["leader_mac"] | ""));
    cfg.timezone = String((const char*)(doc["timezone"] | cfg.timezone.c_str()));
    cfg.ap_ssid = String((const char*)(doc["ap_ssid"] | cfg.ap_ssid.c_str()));
    cfg.ap_pass = String((const char*)(doc["ap_pass"] | cfg.ap_pass.c_str()));
    cfg.sensor_type = String((const char*)(doc["sensor_type"] | cfg.sensor_type.c_str()));
    cfg.follower_wait_sync_ms = doc["follower_wait_sync_ms"] | cfg.follower_wait_sync_ms;

    if (doc["gpio_pins"].is<JsonObject>()) {
        cfg.gpio_pins.i2c_sda = doc["gpio_pins"]["i2c_sda"] | cfg.gpio_pins.i2c_sda;
        cfg.gpio_pins.i2c_scl = doc["gpio_pins"]["i2c_scl"] | cfg.gpio_pins.i2c_scl;
        cfg.gpio_pins.battery_adc = doc["gpio_pins"]["battery_adc"] | cfg.gpio_pins.battery_adc;
    }

    if (doc["battery"].is<JsonObject>()) {
        cfg.battery.divider_r1 = doc["battery"]["divider_r1"] | cfg.battery.divider_r1;
        cfg.battery.divider_r2 = doc["battery"]["divider_r2"] | cfg.battery.divider_r2;
        cfg.battery.adc_vref = doc["battery"]["adc_vref"] | cfg.battery.adc_vref;
        cfg.battery.adc_max = doc["battery"]["adc_max"] | cfg.battery.adc_max;
        cfg.battery.calibration_factor = doc["battery"]["calibration_factor"] | cfg.battery.calibration_factor;
    }

    cfg.report_times.clear();
    if (doc["report_times"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["report_times"].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
            const char* s = v.as<const char*>();
            if (s) {
                cfg.report_times.push_back(String(s));
            }
        }
    }

    if (cfg.report_times.empty()) {
        cfg.report_times.push_back("10:00");
        cfg.report_times.push_back("22:00");
    }

    return true;
}

void ConfigManager::configToJson(const AppConfig& cfg, JsonDocument& doc) {
    doc["role"] = cfg.role;
    doc["id"] = cfg.id;
    doc["device_name"] = cfg.device_name;
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["wifi_pass"] = cfg.wifi_pass;
    doc["wifi_channel"] = cfg.wifi_channel;
    doc["server_url"] = cfg.server_url;
    doc["leader_mac"] = cfg.leader_mac;
    doc["timezone"] = cfg.timezone;
    doc["ap_ssid"] = cfg.ap_ssid;
    doc["ap_pass"] = cfg.ap_pass;
    doc["sensor_type"] = cfg.sensor_type;
    doc["follower_wait_sync_ms"] = cfg.follower_wait_sync_ms;

    JsonObject gp = doc["gpio_pins"].to<JsonObject>();
    gp["i2c_sda"] = cfg.gpio_pins.i2c_sda;
    gp["i2c_scl"] = cfg.gpio_pins.i2c_scl;
    gp["battery_adc"] = cfg.gpio_pins.battery_adc;

    JsonObject batt = doc["battery"].to<JsonObject>();
    batt["divider_r1"] = cfg.battery.divider_r1;
    batt["divider_r2"] = cfg.battery.divider_r2;
    batt["adc_vref"] = cfg.battery.adc_vref;
    batt["adc_max"] = cfg.battery.adc_max;
    batt["calibration_factor"] = cfg.battery.calibration_factor;

    JsonArray arr = doc["report_times"].to<JsonArray>();
    for (const auto& t : cfg.report_times) arr.add(t);
}