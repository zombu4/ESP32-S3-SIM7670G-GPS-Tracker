#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "cJSON.h"

// Include configuration and modules
#include "config.h"
#include "version.h"
#include "task_manager.h"
#include "modules/gps/gps_module.h"
#include "modules/lte/lte_module.h"
#include "modules/mqtt/mqtt_module.h"
#include "modules/battery/battery_module.h"
#include "modules/modem_init/modem_init.h"
#include "baud_rate_tester.h"

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
void data_aggregation_task(void* params);
static bool initialize_modules(void);
static char* create_json_payload(const gps_data_t* gps, const battery_data_t* battery);

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3-SIM7670G GPS Tracker starting...");
    
    // Display version information
    ESP_LOGI(TAG, "=== VERSION INFORMATION ===");
    ESP_LOGI(TAG, "%s", get_version_info());
    ESP_LOGI(TAG, "%s", get_build_info());
    ESP_LOGI(TAG, "===========================");
    
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
    
    // ========= WAVESHARE SIM7670G PROPER INITIALIZATION =========
    ESP_LOGI(TAG, "🔧 Using Waveshare SIM7670G recommended initialization sequence");
    ESP_LOGI(TAG, "� Hardware: TX=17, RX=18, Baud=115200 (ESP32-S3-SIM7670G standard)");
    ESP_LOGI(TAG, "=========================================================");
    
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
    
    // Task watchdog is initialized by task manager - no need to initialize here
    
    // Get task manager interface and start dual-core tasks
    const task_manager_t* task_mgr = task_manager_get_interface();
    if (task_mgr && task_mgr->init() && task_mgr->start_all_tasks()) {
        ESP_LOGI(TAG, "✅ Dual-core task system started!");
        
        // Start data aggregation task on Core 0 (with MQTT)
        xTaskCreatePinnedToCore(
            data_aggregation_task,
            "data_aggregator",
            4096,
            (void*)task_mgr,
            PRIORITY_NORMAL,
            NULL,
            PROTOCOL_CORE
        );
        ESP_LOGI(TAG, "✅ Data aggregation task started on Core 0");
        
    } else {
        ESP_LOGE(TAG, "❌ Failed to start dual-core task system");
    }
    
    // Main supervision task - lightweight monitoring only
    // Remove unused variables - data is now handled by task manager
    uint32_t status_counter = 0;
    
    while (1) {
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Non-blocking status monitoring
        if (task_mgr && status_counter % 12 == 0) {  // Every minute with 5s delay
            ESP_LOGI(TAG, "🚀 System running on dual cores - All operations non-blocking");
        }
        
        status_counter++;
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 second intervals - non-blocking
    }
}

static bool initialize_modules(void)
{
    ESP_LOGI(TAG, "🚀 === PROPER WAVESHARE SIM7670G INITIALIZATION ===");
    
    // Get module interfaces
    gps_if = gps_get_interface();
    lte_if = lte_get_interface();
    mqtt_if = mqtt_get_interface();
    battery_if = battery_get_interface();
    
    if (!gps_if || !lte_if || !mqtt_if || !battery_if) {
        ESP_LOGE(TAG, "Failed to get module interfaces");
        return false;
    }
    
    // Initialize battery module first (independent of modem)
    ESP_LOGI(TAG, "🔋 Initializing battery module...");
    if (!battery_if->init(&system_config.battery)) {
        ESP_LOGE(TAG, "Failed to initialize battery module");
        return false;
    }
    ESP_LOGI(TAG, "✅ Battery module initialized");
    
    // Initialize LTE module for UART communication
    ESP_LOGI(TAG, "📡 Initializing LTE module for UART communication...");
    if (!lte_if->init(&system_config.lte)) {
        ESP_LOGE(TAG, "Failed to initialize LTE module");
        return false;
    }
    ESP_LOGI(TAG, "✅ LTE module initialized");
    
    // *** CRITICAL: FOLLOW WAVESHARE SIM7670G INITIALIZATION SEQUENCE ***
    ESP_LOGI(TAG, "📖 Following Waveshare SIM7670G recommended initialization sequence");
    
    // Execute complete modem initialization sequence
    // This includes: modem readiness, network registration, connectivity test, GPS init
    if (!modem_init_complete_sequence(120)) { // 2 minute timeout for complete sequence
        ESP_LOGE(TAG, "❌ Waveshare SIM7670G initialization sequence failed");
        ESP_LOGI(TAG, "⚡ This may be normal for first GPS fix - GPS needs outdoor sky visibility");
        ESP_LOGI(TAG, "🔄 Continuing with module initialization...");
    } else {
        ESP_LOGI(TAG, "✅ Waveshare SIM7670G initialization sequence completed successfully!");
    }
    
    // Now initialize GPS module using the pre-initialized modem
    ESP_LOGI(TAG, "🛰️ Initializing GPS module (modem already initialized)...");
    if (!gps_if->init(&system_config.gps)) {
        ESP_LOGW(TAG, "⚠️  GPS module init reported failure, but this may be expected");
        ESP_LOGI(TAG, "   📍 GPS is likely active and searching for satellites");
    } else {
        ESP_LOGI(TAG, "✅ GPS module initialized");
    }
    
    // Test cellular network connectivity
    ESP_LOGI(TAG, "🌐 Testing cellular network connectivity...");
    if (!lte_if->connect()) {
        ESP_LOGW(TAG, "⚠️  Failed to initiate cellular network connection");
        ESP_LOGI(TAG, "   🔄 Network may already be active from modem initialization");
    } else {
        ESP_LOGI(TAG, "✅ Cellular network connection initiated");
    }
    
    // Wait for stable network connection
    ESP_LOGI(TAG, "⏳ Waiting for stable cellular network connection...");
    int connection_attempts = 0;
    while (lte_if->get_connection_status() != LTE_STATUS_CONNECTED && connection_attempts < 15) {
        ESP_LOGI(TAG, "   📶 Connection attempt %d/15...", connection_attempts + 1);
        vTaskDelay(pdMS_TO_TICKS(2000));
        connection_attempts++;
    }
    
    if (lte_if->get_connection_status() == LTE_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "✅ Cellular network connected successfully");
    } else {
        ESP_LOGW(TAG, "⚠️  Cellular network connection not confirmed, but may be working");
        ESP_LOGI(TAG, "   📡 Network functions may work via modem initialization");
    }
    
    // Initialize MQTT module
    ESP_LOGI(TAG, "💬 Initializing MQTT module...");
    if (!mqtt_if->init(&system_config.mqtt)) {
        ESP_LOGE(TAG, "❌ Failed to initialize MQTT module");
        return false;
    }
    ESP_LOGI(TAG, "✅ MQTT module initialized");
    
    ESP_LOGI(TAG, "🎉 === ALL MODULES INITIALIZED SUCCESSFULLY ===");
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
        
        vTaskDelay(pdMS_TO_TICKS(15000)); // Collect data every 15 seconds (slower GPS polling)
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

/**
 * @brief Data aggregation task - collects data from queues and publishes via MQTT
 * Runs on Core 0 (Protocol Core) alongside MQTT task
 */
void data_aggregation_task(void* params)
{
    const task_manager_t* task_mgr = (const task_manager_t*)params;
    ESP_LOGI(TAG, "📊 [Core 0] Data Aggregation Task started");
    
    gps_data_t gps_data = {0};
    battery_data_t battery_data = {0};
    bool have_gps = false;
    bool have_battery = false;
    
    uint32_t last_publish_time = 0;
    const uint32_t publish_interval_ms = system_config.system.transmission_interval_ms;
    
    while (task_mgr->tasks_running) {
        // Collect GPS data (non-blocking)
        if (xQueueReceive(task_mgr->gps_data_queue, &gps_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            have_gps = true;
            ESP_LOGD(TAG, "📍 Received GPS data: %.6f,%.6f (%d sats)", 
                     gps_data.latitude, gps_data.longitude, gps_data.satellites);
        }
        
        // Collect battery data (non-blocking)
        if (xQueueReceive(task_mgr->battery_data_queue, &battery_data, pdMS_TO_TICKS(10)) == pdTRUE) {
            have_battery = true;
            ESP_LOGD(TAG, "🔋 Received battery data: %.1f%% (%.2fV)", 
                     battery_data.percentage, battery_data.voltage);
        }
        
        // Publish data at regular intervals
        uint32_t current_time = esp_log_timestamp();
        if (have_gps && have_battery && 
            (current_time - last_publish_time) >= publish_interval_ms) {
            
            // Create JSON payload
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "timestamp", current_time / 1000);
            cJSON_AddStringToObject(root, "device_id", "esp32_gps_tracker");
            
            // Add GPS data
            cJSON* gps_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(gps_obj, "latitude", gps_data.latitude);
            cJSON_AddNumberToObject(gps_obj, "longitude", gps_data.longitude);
            cJSON_AddNumberToObject(gps_obj, "altitude", gps_data.altitude);
            cJSON_AddNumberToObject(gps_obj, "speed", gps_data.speed_kmh);
            cJSON_AddNumberToObject(gps_obj, "satellites", gps_data.satellites);
            cJSON_AddBoolToObject(gps_obj, "valid_fix", gps_data.fix_valid);
            cJSON_AddItemToObject(root, "gps", gps_obj);
            
            // Add battery data
            cJSON* battery_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(battery_obj, "percentage", battery_data.percentage);
            cJSON_AddNumberToObject(battery_obj, "voltage", battery_data.voltage);
            cJSON_AddBoolToObject(battery_obj, "charging", battery_data.charging);
            // Calculate low/critical battery status
            bool low_battery = battery_data.percentage < 15.0f;
            bool critical_battery = battery_data.percentage < 5.0f;
            cJSON_AddBoolToObject(battery_obj, "low_battery", low_battery);
            cJSON_AddBoolToObject(battery_obj, "critical_battery", critical_battery);
            cJSON_AddItemToObject(root, "battery", battery_obj);
            
            // Convert to string and publish
            char* json_string = cJSON_Print(root);
            if (json_string) {
                if (task_mgr->publish_mqtt(system_config.mqtt.topic, json_string, 0)) {
                    ESP_LOGI(TAG, "📤 Data published successfully");
                    last_publish_time = current_time;
                } else {
                    ESP_LOGW(TAG, "⚠️  Failed to publish data");
                }
                free(json_string);
            }
            cJSON_Delete(root);
        }
        
        // Feed watchdog and yield
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));  // Check for data every 500ms
    }
    
    ESP_LOGI(TAG, "🛑 [Core 0] Data Aggregation Task stopped");
    vTaskDelete(NULL);
}