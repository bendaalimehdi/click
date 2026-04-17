#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct GpioPinsConfig {
    int i2c_sda = 7;
    int i2c_scl = 6;
    int battery_adc = 5;
};

struct BatteryConfig {
    float divider_r1 = 1000000.0f;
    float divider_r2 = 1000000.0f;
    float adc_vref = 3.3f;
    float adc_max = 4095.0f;
    float calibration_factor = 1.0f;
};

struct AppConfig {
    String role = "leader";
    int id = 1;
    String device_name = "NODE";
    String wifi_ssid;
    String wifi_pass;
    uint8_t wifi_channel = 1;
    String server_url;
    String leader_mac;
    String timezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    String ap_ssid = "MANAGER_LEADER";
    String ap_pass = "12345678";
    String sensor_type = "bmp280";
    uint32_t follower_wait_sync_ms = 500;
    GpioPinsConfig gpio_pins;
    BatteryConfig battery;
    std::vector<String> report_times;
    String report_mode = "interval";
    uint16_t report_interval_min = 60;
};

class ConfigManager {
public:
    bool begin();
    bool load(AppConfig& cfg);
    bool save(const AppConfig& cfg);
    bool saveFromJson(const JsonDocument& doc);
    String getLastError() const;

private:
    bool applyJsonToConfig(const JsonDocument& doc, AppConfig& cfg);
    void configToJson(const AppConfig& cfg, JsonDocument& doc);
    String _lastError;
};