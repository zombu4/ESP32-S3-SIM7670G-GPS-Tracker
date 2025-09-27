#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"

// Try to include user-specific configuration
#ifdef __has_include
 #if __has_include("config_user.h")
 #include "config_user.h"
 #define HAS_USER_CONFIG
 #endif
#endif

// Force user config for development
#ifndef HAS_USER_CONFIG
 #include "config_user.h"
 #define HAS_USER_CONFIG
#endif

static const char *TAG = "CONFIG";
static const char *NVS_NAMESPACE = "tracker_cfg";

static tracker_system_config_t default_config = {
 // Hardware Configuration
 .uart_hw = {
 .uart_num = 1, // UART_NUM_1
 .tx_pin = 17,
 .rx_pin = 18,
 .baud_rate = 115200,
 .buffer_size = 1024
 },
 
 .i2c_hw = {
 .i2c_num = 0, // I2C_NUM_0
 .sda_pin = 3,
 .scl_pin = 2,
 .frequency_hz = 100000
 },
 
 // GPS Module Configuration
 .gps = {
 .enabled = true,
 .fix_timeout_ms = 60000,
 .min_satellites = 3, // Lowered from 4 to 3 for better fix chances
 .data_update_interval_ms = 30000, // 30-second polling
 .debug_nmea = true,
 .debug_output = true // Enable verbose output for troubleshooting
 },
 
 // LTE Module Configuration
 .lte = {
 .enabled = true,
#ifdef HAS_USER_CONFIG
 .apn = USER_CONFIG_APN,
 .username = USER_CONFIG_APN_USERNAME,
 .password = USER_CONFIG_APN_PASSWORD,
#else
 .apn = "your-apn-here", // CONFIGURE: Set your cellular APN in config_user.h
 .username = "",
 .password = "",
#endif
 .network_timeout_ms = 30000,
 .max_retries = 5,
 .debug_at_commands = true, // Enable AT command debugging
 .debug_output = true // Enable verbose output for troubleshooting
 },
 
 // MQTT Module Configuration
 .mqtt = {
 .enabled = true,
#ifdef HAS_USER_CONFIG
 .broker_host = USER_CONFIG_MQTT_BROKER,
 .broker_port = USER_CONFIG_MQTT_PORT,
 .client_id = USER_CONFIG_MQTT_CLIENT_ID,
 .topic = USER_CONFIG_MQTT_TOPIC,
 .username = USER_CONFIG_MQTT_USERNAME,
 .password = USER_CONFIG_MQTT_PASSWORD,
 .enable_ssl = USER_CONFIG_MQTT_ENABLE_SSL,
#else
 .broker_host = "65.124.194.3", // User's MQTT broker
 .broker_port = 1883,
 .client_id = "ESP32GPS", // Will be made unique with MAC address
 .topic = "gps-tracker", // Updated topic as requested by user 
 .username = "", // No username required
 .password = "", // No password required
 .enable_ssl = false, // Default to no SSL
#endif
 .keepalive_sec = 60,
 .qos_level = 0,
 .max_retries = 3,
 .retain_messages = false,
 .debug_output = true // Enable verbose output for troubleshooting
 },
 
 // Battery Module Configuration
 .battery = {
 .enabled = true,
 .low_battery_threshold = 10.0f,
 .critical_battery_threshold = 5.0f,
 .read_interval_ms = 10000,
 .enable_charging_detection = true,
 .debug_output = true // Enable verbose output for troubleshooting
 },
 
 // System Configuration
 .system = {
 .data_collection_interval_ms = 5000,
 .transmission_interval_ms = 30000, // 30 seconds for MQTT transmission - faster testing
 .gps_polling_interval_ms = 25000, // 25 seconds - collect fresh GPS data before MQTT
 .system_status_interval_ms = 60000,
 .enable_watchdog = true,
 .enable_deep_sleep = false,
 .deep_sleep_duration_ms = 300000, // 5 minutes
 .debug_system = true // Enable verbose output for troubleshooting
 }
};

tracker_system_config_t* config_get_default(void)
{
 return &default_config;
}

bool config_load_from_nvs(tracker_system_config_t* config)
{
 if (!config) {
 ESP_LOGE(TAG, "Config pointer is NULL");
 return false;
 }
 
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "NVS namespace not found, will create on first save - using defaults");
        } else {
            ESP_LOGW(TAG, "Failed to open NVS namespace (%s), using defaults", esp_err_to_name(err));
        }
        *config = default_config;
        return false;
    } size_t required_size = sizeof(tracker_system_config_t);
 err = nvs_get_blob(nvs_handle, "config", config, &required_size);
 nvs_close(nvs_handle);
 
 if (err != ESP_OK) {
 ESP_LOGW(TAG, "Failed to read config from NVS, using defaults");
 *config = default_config;
 return false;
 }
 
 // Validate loaded configuration
 if (!config_validate(config)) {
 ESP_LOGW(TAG, "Invalid configuration loaded, using defaults");
 *config = default_config;
 return false;
 }
 
 ESP_LOGI(TAG, "Configuration loaded from NVS");
 return true;
}

bool config_save_to_nvs(const tracker_system_config_t* config)
{
 if (!config || !config_validate(config)) {
 ESP_LOGE(TAG, "Invalid configuration, cannot save");
 return false;
 }
 
 nvs_handle_t nvs_handle;
 esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "Failed to open NVS namespace for writing");
 return false;
 }
 
 err = nvs_set_blob(nvs_handle, "config", config, sizeof(tracker_system_config_t));
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "Failed to write config to NVS");
 nvs_close(nvs_handle);
 return false;
 }
 
 err = nvs_commit(nvs_handle);
 nvs_close(nvs_handle);
 
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "Failed to commit config to NVS");
 return false;
 }
 
 ESP_LOGI(TAG, "Configuration saved to NVS");
 return true;
}

bool config_validate(const tracker_system_config_t* config)
{
 if (!config) {
 return false;
 }
 
 // Validate hardware configuration
 if (config->uart_hw.baud_rate < 9600 || config->uart_hw.baud_rate > 921600) {
 ESP_LOGE(TAG, "Invalid UART baud rate: %d", config->uart_hw.baud_rate);
 return false;
 }
 
 if (config->i2c_hw.frequency_hz < 10000 || config->i2c_hw.frequency_hz > 1000000) {
 ESP_LOGE(TAG, "Invalid I2C frequency: %d", config->i2c_hw.frequency_hz);
 return false;
 }
 
 // Validate network configuration
 if (strlen(config->lte.apn) == 0) {
 ESP_LOGE(TAG, "APN cannot be empty");
 return false;
 }
 
 if (strlen(config->mqtt.broker_host) == 0) {
 ESP_LOGE(TAG, "MQTT broker host cannot be empty");
 return false;
 }
 
 if (config->mqtt.broker_port < 1 || config->mqtt.broker_port > 65535) {
 ESP_LOGE(TAG, "Invalid MQTT port: %d", config->mqtt.broker_port);
 return false;
 }
 
 // Validate timing configuration
 if (config->system.transmission_interval_ms < 1000) {
 ESP_LOGE(TAG, "Transmission interval too short: %d ms", config->system.transmission_interval_ms);
 return false;
 }
 
 return true;
}

void config_print(const tracker_system_config_t* config)
{
 if (!config) {
 ESP_LOGE(TAG, "Config is NULL");
 return;
 }
 
 ESP_LOGI(TAG, "=== GPS Tracker Configuration ===");
 
 ESP_LOGI(TAG, "Hardware:");
 ESP_LOGI(TAG, " UART: num=%d, TX=%d, RX=%d, baud=%d", 
 config->uart_hw.uart_num, config->uart_hw.tx_pin, 
 config->uart_hw.rx_pin, config->uart_hw.baud_rate);
 ESP_LOGI(TAG, " I2C: num=%d, SDA=%d, SCL=%d, freq=%d", 
 config->i2c_hw.i2c_num, config->i2c_hw.sda_pin, 
 config->i2c_hw.scl_pin, config->i2c_hw.frequency_hz);
 
 ESP_LOGI(TAG, "GPS: enabled=%s, timeout=%d ms, min_sats=%d", 
 config->gps.enabled ? "yes" : "no", 
 config->gps.fix_timeout_ms, config->gps.min_satellites);
 
 ESP_LOGI(TAG, "LTE: enabled=%s, APN='%s', timeout=%d ms", 
 config->lte.enabled ? "yes" : "no", 
 config->lte.apn, config->lte.network_timeout_ms);
 
 ESP_LOGI(TAG, "MQTT: enabled=%s, broker=%s:%d, topic='%s', SSL=%s", 
 config->mqtt.enabled ? "yes" : "no", 
 config->mqtt.broker_host, config->mqtt.broker_port, 
 config->mqtt.topic, config->mqtt.enable_ssl ? "yes" : "no");
 
 ESP_LOGI(TAG, "Battery: enabled=%s, low=%.1f%%, critical=%.1f%%", 
 config->battery.enabled ? "yes" : "no", 
 config->battery.low_battery_threshold, 
 config->battery.critical_battery_threshold);
 
 ESP_LOGI(TAG, "System: data_interval=%d ms, tx_interval=%d ms", 
 config->system.data_collection_interval_ms, 
 config->system.transmission_interval_ms);
}

bool config_update_mqtt_broker(tracker_system_config_t* config, const char* host, int port)
{
 if (!config || !host || port < 1 || port > 65535) {
 return false;
 }
 
 strncpy(config->mqtt.broker_host, host, sizeof(config->mqtt.broker_host) - 1);
 config->mqtt.broker_host[sizeof(config->mqtt.broker_host) - 1] = '\0';
 config->mqtt.broker_port = port;
 
 ESP_LOGI(TAG, "Updated MQTT broker to %s:%d", host, port);
 return true;
}

bool config_update_lte_apn(tracker_system_config_t* config, const char* apn)
{
 if (!config || !apn) {
 return false;
 }
 
 strncpy(config->lte.apn, apn, sizeof(config->lte.apn) - 1);
 config->lte.apn[sizeof(config->lte.apn) - 1] = '\0';
 
 ESP_LOGI(TAG, "Updated LTE APN to '%s'", apn);
 return true;
}

bool config_update_transmission_interval(tracker_system_config_t* config, int interval_ms)
{
 if (!config || interval_ms < 1000) {
 return false;
 }
 
 config->system.transmission_interval_ms = interval_ms;
 ESP_LOGI(TAG, "Updated transmission interval to %d ms", interval_ms);
 return true;
}

bool config_update_mqtt_ssl(tracker_system_config_t* config, bool enable_ssl)
{
 if (!config) {
 return false;
 }
 
 config->mqtt.enable_ssl = enable_ssl;
 ESP_LOGI(TAG, "Updated MQTT SSL to %s", enable_ssl ? "enabled" : "disabled");
 return true;
}

bool config_update_mqtt_auth(tracker_system_config_t* config, const char* username, const char* password)
{
 if (!config) {
 return false;
 }
 
 // Handle empty/null credentials (for open MQTT brokers)
 if (username) {
 strncpy(config->mqtt.username, username, sizeof(config->mqtt.username) - 1);
 config->mqtt.username[sizeof(config->mqtt.username) - 1] = '\0';
 } else {
 config->mqtt.username[0] = '\0'; // Clear username
 }
 
 if (password) {
 strncpy(config->mqtt.password, password, sizeof(config->mqtt.password) - 1);
 config->mqtt.password[sizeof(config->mqtt.password) - 1] = '\0';
 } else {
 config->mqtt.password[0] = '\0'; // Clear password
 }
 
 ESP_LOGI(TAG, "Updated MQTT authentication (username=%s, password=%s)", 
 strlen(config->mqtt.username) > 0 ? "set" : "empty",
 strlen(config->mqtt.password) > 0 ? "set" : "empty");
 return true;
}