#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "cJSON.h"

// Include configuration and modules
#include "config.h"
#include "modules/gps/gps_module.h"
#include "modules/lte/lte_module.h"
#include "modules/mqtt/mqtt_module.h"
#include "modules/battery/battery_module.h"

static const char *TAG = "GPS_TRACKER";

// Module interfaces
static const gps_interface_t* gps_if = NULL;
static const lte_interface_t* lte_if = NULL;
static const mqtt_interface_t* mqtt_if = NULL;
static const battery_interface_t* battery_if = NULL;

// System configuration
static tracker_system_config_t system_config = {0};

static TimerHandle_t transmission_timer;
static gps_data_t last_gps_data = {0};
static battery_data_t last_battery_data = {0};

// Function prototypes
static void transmission_timer_callback(TimerHandle_t xTimer);
static void data_collection_task(void *pvParameters);
static bool initialize_modules(void);
static char* create_json_payload(const gps_data_t* gps, const battery_data_t* battery);

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3-SIM7670G GPS Tracker starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Load system configuration
    if (!config_load_from_nvs(&system_config)) {
        ESP_LOGW(TAG, "Failed to load config from NVS, using defaults");
        tracker_system_config_t* defaults = config_get_default();
        memcpy(&system_config, defaults, sizeof(tracker_system_config_t));
    }
    
    // Initialize all modules
    if (!initialize_modules()) {
        ESP_LOGE(TAG, "Failed to initialize modules");
        return;
    }
    
    // Create data collection task
    xTaskCreate(data_collection_task, "data_collection", 4096, NULL, 5, NULL);
    
    // Create transmission timer
    transmission_timer = xTimerCreate("TransmissionTimer", 
                                      pdMS_TO_TICKS(system_config.system.transmission_interval_ms),
                                      pdTRUE, 
                                      (void*)0, 
                                      transmission_timer_callback);
    
    if (transmission_timer != NULL) {
        xTimerStart(transmission_timer, 0);
        ESP_LOGI(TAG, "Transmission timer started (interval: %d ms)", 
                 system_config.system.transmission_interval_ms);
    }
    
    ESP_LOGI(TAG, "GPS Tracker initialization complete");
    
    // Main task monitors system health and handles events
    while (1) {
        ESP_LOGI(TAG, "System running - GPS: %s, Battery: %.1f%%", 
                 last_gps_data.fix_valid ? "VALID" : "NO FIX",
                 last_battery_data.percentage);
        
        // Check for critical battery level
        if (last_battery_data.present && last_battery_data.percentage <= 5.0f) {
            ESP_LOGW(TAG, "Critical battery level detected!");
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Log status every minute
    }
}

static bool initialize_modules(void)
{
    ESP_LOGI(TAG, "Initializing modules...");
    
    // Get module interfaces
    gps_if = gps_get_interface();
    lte_if = lte_get_interface();
    mqtt_if = mqtt_get_interface();
    battery_if = battery_get_interface();
    
    if (!gps_if || !lte_if || !mqtt_if || !battery_if) {
        ESP_LOGE(TAG, "Failed to get module interfaces");
        return false;
    }
    
    // Initialize battery module first
    if (!battery_if->init(&system_config.battery)) {
        ESP_LOGE(TAG, "Failed to initialize battery module");
        return false;
    }
    ESP_LOGI(TAG, "Battery module initialized");
    
    // Initialize GPS module
    if (!gps_if->init(&system_config.gps)) {
        ESP_LOGE(TAG, "Failed to initialize GPS module");
        return false;
    }
    ESP_LOGI(TAG, "GPS module initialized");
    
    // Initialize LTE module
    if (!lte_if->init(&system_config.lte)) {
        ESP_LOGE(TAG, "Failed to initialize LTE module");
        return false;
    }
    ESP_LOGI(TAG, "LTE module initialized");
    
    // Wait for network connection
    ESP_LOGI(TAG, "Waiting for cellular network connection...");
    int connection_attempts = 0;
    while (lte_if->get_connection_status() != LTE_STATUS_CONNECTED && connection_attempts < 30) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        connection_attempts++;
    }
    
    if (lte_if->get_connection_status() != LTE_STATUS_CONNECTED) {
        ESP_LOGE(TAG, "Failed to connect to cellular network after 60 seconds");
        return false;
    }
    ESP_LOGI(TAG, "Cellular network connected");
    
    // Initialize MQTT module
    if (!mqtt_if->init(&system_config.mqtt)) {
        ESP_LOGE(TAG, "Failed to initialize MQTT module");
        return false;
    }
    ESP_LOGI(TAG, "MQTT module initialized");
    
    return true;
}

static void data_collection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Data collection task started");
    
    while (1) {
        // Read GPS data
        if (gps_if && !gps_if->read_data(&last_gps_data)) {
            ESP_LOGW(TAG, "Failed to read GPS data");
            memset(&last_gps_data, 0, sizeof(gps_data_t));
        }
        
        // Read battery data
        if (battery_if && !battery_if->read_data(&last_battery_data)) {
            ESP_LOGW(TAG, "Failed to read battery data");
            memset(&last_battery_data, 0, sizeof(battery_data_t));
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Collect data every 5 seconds
    }
}

static void transmission_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Transmission timer triggered");
    
    if (!mqtt_if) {
        ESP_LOGE(TAG, "MQTT interface not available");
        return;
    }
    
    // Create JSON payload
    char* payload = create_json_payload(&last_gps_data, &last_battery_data);
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return;
    }
    
    // Publish data
    mqtt_publish_result_t result = {0};
    if (mqtt_if->publish_json(system_config.mqtt.topic, payload, &result)) {
        ESP_LOGI(TAG, "Data transmitted successfully");
    } else {
        ESP_LOGW(TAG, "Failed to transmit data");
    }
    
    free(payload);
}

static char* create_json_payload(const gps_data_t* gps, const battery_data_t* battery)
{
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    
    // Add timestamp
    cJSON* timestamp = cJSON_CreateString(gps->timestamp[0] ? gps->timestamp : "unknown");
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    
    // Add GPS data
    cJSON* gps_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(gps_obj, "latitude", gps->latitude);
    cJSON_AddNumberToObject(gps_obj, "longitude", gps->longitude);
    cJSON_AddNumberToObject(gps_obj, "altitude", gps->altitude);
    cJSON_AddNumberToObject(gps_obj, "speed_kmh", gps->speed_kmh);
    cJSON_AddNumberToObject(gps_obj, "course", gps->course);
    cJSON_AddNumberToObject(gps_obj, "satellites", gps->satellites);
    cJSON_AddBoolToObject(gps_obj, "fix_valid", gps->fix_valid);
    cJSON_AddItemToObject(root, "gps", gps_obj);
    
    // Add battery data
    cJSON* battery_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(battery_obj, "percentage", battery->percentage);
    cJSON_AddNumberToObject(battery_obj, "voltage", battery->voltage);
    cJSON_AddBoolToObject(battery_obj, "charging", battery->charging);
    cJSON_AddBoolToObject(battery_obj, "present", battery->present);
    cJSON_AddItemToObject(root, "battery", battery_obj);
    
    // Convert to string
    char* json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    return json_string;
}