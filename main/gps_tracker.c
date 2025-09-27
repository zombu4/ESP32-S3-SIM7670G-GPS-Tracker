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
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "soc/soc.h"

// Include configuration and modules
#include "config.h"
#include "version.h"
#include "task_system.h"
#include "multitask_manager.h"
#include "modules/gps/gps_module.h"
#include "modules/lte/lte_module.h"
#include "modules/mqtt/mqtt_module.h"
#include "modules/battery/battery_module.h"
#include "modules/modem_init/modem_init.h"
#include "baud_rate_tester.h"

// External function declarations for MQTT JSON payload creation
extern bool mqtt_create_json_payload(const char* latitude, const char* longitude, 
 float battery_voltage, int battery_percentage,
 char* json_buffer, size_t buffer_size);
extern bool mqtt_create_enhanced_json_payload(const gps_data_t* gps_data, const battery_data_t* battery_data,
 bool fresh_gps_data, char* json_buffer, size_t buffer_size);

static const char *TAG = "GPS_TRACKER";

// Module interfaces
static const gps_interface_t* gps_if = NULL;
static const lte_interface_t* lte_if = NULL;
static const mqtt_interface_t* mqtt_if = NULL;
static const battery_interface_t* battery_if = NULL;

// System configuration
tracker_system_config_t system_config = {0};

static TimerHandle_t transmission_timer;
static TimerHandle_t gps_polling_timer;
static gps_data_t last_gps_data = {0};
static battery_data_t last_battery_data = {0};
static bool fresh_gps_data_available = false;

// SAFE: Timer flags - only set from timer callbacks, read by tasks
static volatile bool gps_polling_requested = false;
static volatile bool mqtt_transmission_requested = false;

// === GPS DATA BUFFER SYSTEM ===
// Solves GPS NMEA interference by decoupling GPS polling from MQTT transmission
typedef struct {
    gps_data_t gps_data;
    battery_data_t battery_data;
    bool valid;
    bool consumed;
    uint32_t timestamp;
} gps_buffer_t;

static gps_buffer_t gps_data_buffer = {0};
static SemaphoreHandle_t buffer_mutex = NULL;

// Function prototypes (some commented out for task system migration)
static void transmission_timer_callback(TimerHandle_t xTimer);
static void gps_polling_timer_callback(TimerHandle_t xTimer);
static bool transmit_gps_data_via_mqtt(void);
// void data_aggregation_task(void* params); // Replaced by individual tasks

// Function to get current MQTT configuration for task manager
const mqtt_config_t* gps_tracker_get_mqtt_config(void)
{
 return &system_config.mqtt;
}
// static bool initialize_modules(void); // Replaced by task system
static bool collect_and_parse_gps_data(void);

// === GPS DATA BUFFER MANAGEMENT ===
// These functions solve GPS NMEA interference by decoupling GPS polling from MQTT

static bool gps_buffer_init(void) {
    buffer_mutex = xSemaphoreCreateMutex();
    if (buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create GPS buffer mutex");
        return false;
    }
    
    memset(&gps_data_buffer, 0, sizeof(gps_buffer_t));
    ESP_LOGI(TAG, "GPS data buffer initialized");
    return true;
}

static bool gps_buffer_store(const gps_data_t* gps_data, const battery_data_t* battery_data) {
    if (!gps_data || !battery_data) {
        return false;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(&gps_data_buffer.gps_data, gps_data, sizeof(gps_data_t));
        memcpy(&gps_data_buffer.battery_data, battery_data, sizeof(battery_data_t));
        gps_data_buffer.valid = true;
        gps_data_buffer.consumed = false;
        gps_data_buffer.timestamp = xTaskGetTickCount();
        
        ESP_LOGI(TAG, "GPS data stored in buffer: Lat=%.6f, Lon=%.6f, Sats=%d, Battery=%.2fV", 
                gps_data->latitude, gps_data->longitude, gps_data->satellites, battery_data->voltage);
        
        xSemaphoreGive(buffer_mutex);
        return true;
    }
    return false;
}

static bool gps_buffer_read(gps_data_t* gps_data, battery_data_t* battery_data) {
    if (!gps_data || !battery_data) {
        return false;
    }
    
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (gps_data_buffer.valid && !gps_data_buffer.consumed) {
            memcpy(gps_data, &gps_data_buffer.gps_data, sizeof(gps_data_t));
            memcpy(battery_data, &gps_data_buffer.battery_data, sizeof(battery_data_t));
            gps_data_buffer.consumed = true; // Mark as consumed
            
            ESP_LOGI(TAG, "GPS data read from buffer: Lat=%.6f, Lon=%.6f, Sats=%d", 
                    gps_data->latitude, gps_data->longitude, gps_data->satellites);
            
            xSemaphoreGive(buffer_mutex);
            return true;
        }
        xSemaphoreGive(buffer_mutex);
    }
    return false;
}

static bool gps_buffer_has_data(void) {
    bool has_data = false;
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        has_data = gps_data_buffer.valid && !gps_data_buffer.consumed;
        xSemaphoreGive(buffer_mutex);
    }
    return has_data;
}

static void gps_buffer_flush(void) {
    if (xSemaphoreTake(buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        gps_data_buffer.valid = false;
        gps_data_buffer.consumed = true;
        ESP_LOGI(TAG, "GPS data buffer flushed");
        xSemaphoreGive(buffer_mutex);
    }
}

void app_main(void)
{
 ESP_LOGI(TAG, "ESP32-S3-SIM7670G GPS Tracker starting...");
 
 // Display version information
 ESP_LOGI(TAG, "=== VERSION INFORMATION ===");
 ESP_LOGI(TAG, "%s", get_version_info());
 ESP_LOGI(TAG, "%s", get_build_info());
 ESP_LOGI(TAG, "===========================");
 
 // Initialize NVS with error handling
 ESP_LOGI(TAG, "🗄️  Initializing NVS flash storage...");
 esp_err_t ret = nvs_flash_init();
 if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
     ESP_LOGW(TAG, "NVS flash needs to be erased, erasing...");
     ESP_ERROR_CHECK(nvs_flash_erase());
     ret = nvs_flash_init();
 }
 if (ret != ESP_OK) {
     ESP_LOGE(TAG, "Failed to initialize NVS flash: %s", esp_err_to_name(ret));
     return;
 }
 ESP_LOGI(TAG, "✅ NVS flash initialized successfully");
 
 // Load system configuration
 if (!config_load_from_nvs(&system_config)) {
     ESP_LOGW(TAG, "Failed to load config from NVS, using defaults");
     tracker_system_config_t* defaults = config_get_default();
     memcpy(&system_config, defaults, sizeof(tracker_system_config_t));
 }
 
 // ========= ESP32-S3 32-BIT DUAL-CORE OPTIMIZATION =========
 ESP_LOGI(TAG, "🚀 === ESP32-S3 32-BIT DUAL-CORE SYSTEM ===");
 ESP_LOGI(TAG, "� CPU: Dual-core 240MHz with dynamic load balancing");
 ESP_LOGI(TAG, "💾 Memory: Internal RAM + 2MB PSRAM optimized allocation");
 ESP_LOGI(TAG, "⚡ Tasks: Smart CPU affinity, cache-aware memory management");
 ESP_LOGI(TAG, "📡 Hardware: TX=17, RX=18, Baud=115200 (ESP32-S3-SIM7670G)");
 ESP_LOGI(TAG, "🎯 Dependencies: Cellular + GPS fix → MQTT pipeline");
 ESP_LOGI(TAG, "===================================================");
 
 // Display enhanced system capabilities
 ESP_LOGI(TAG, "💽 Free internal heap: %lu KB", esp_get_free_internal_heap_size() / 1024);
 size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
 if (psram_size > 0) {
     ESP_LOGI(TAG, "💽 PSRAM total: %lu KB", psram_size / 1024);
     ESP_LOGI(TAG, "💽 PSRAM free: %lu KB", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
 } else {
     ESP_LOGW(TAG, "💽 PSRAM not detected");
 }
 ESP_LOGI(TAG, "⚡ CPU frequency: 240 MHz (ESP32-S3)");
 ESP_LOGI(TAG, "🔄 Cores available: %d", portNUM_PROCESSORS);
 
 // Initialize enhanced multitasking system
 ESP_LOGI(TAG, "🔧 STEP 1: Initializing enhanced multitasking system...");
 if (!multitask_init()) {
     ESP_LOGE(TAG, "❌ Failed to initialize multitasking manager");
     return;
 }
 ESP_LOGI(TAG, "✅ STEP 1 COMPLETE: Multitasking system initialized");
 
 // Initialize the new task system
 ESP_LOGI(TAG, "🔧 STEP 2: Getting task system interface...");
 const task_system_interface_t* task_sys = get_task_system_interface();
 if (!task_sys) {
     ESP_LOGE(TAG, "❌ Failed to get task system interface");
     return;
 }
 ESP_LOGI(TAG, "✅ STEP 2 COMPLETE: Task system interface acquired");
 
 ESP_LOGI(TAG, "🔧 STEP 3: Initializing task system...");
 if (!task_sys->init()) {
     ESP_LOGE(TAG, "❌ Failed to initialize task system");
     return;
 }
 ESP_LOGI(TAG, "✅ STEP 3 COMPLETE: Task system initialized successfully");
 
 // Enable dynamic CPU affinity for load balancing
 ESP_LOGI(TAG, "🔧 STEP 4: Enabling advanced features...");
 task_sys->enable_dynamic_affinity(true);
 task_sys->enable_auto_recovery(true);
 ESP_LOGI(TAG, "✅ STEP 4 COMPLETE: Dynamic affinity and auto-recovery enabled");
 
 // Start all tasks - MQTT will be started automatically when conditions are met
 ESP_LOGI(TAG, "🔧 STEP 5: Starting all system tasks...");
 if (!task_sys->start_all_tasks()) {
     ESP_LOGE(TAG, "❌ Failed to start task system");
     return;
 }
 ESP_LOGI(TAG, "✅ STEP 5 COMPLETE: All system tasks started successfully");
 
 ESP_LOGI(TAG, "🎉 Task System Architecture Activated!");
 ESP_LOGI(TAG, "📱 Cellular Task: Managing network connectivity");
 ESP_LOGI(TAG, "🛰️  GPS Task: Acquiring satellite fix"); 
 ESP_LOGI(TAG, "📨 MQTT Task: Will start when cellular + GPS ready");
 ESP_LOGI(TAG, "🔋 Battery Task: Monitoring power status");
 ESP_LOGI(TAG, "🔍 Monitor Task: Overseeing system health");
 
 // Wait for system to be ready (optional timeout)
 ESP_LOGI(TAG, "⏳ Waiting for system to become fully operational...");
 
 // Add comprehensive system monitoring and verbose logging
 ESP_LOGI(TAG, "🔍 === SYSTEM STARTUP MONITORING ===");
 ESP_LOGI(TAG, "📊 Task System Interface: %s", task_sys ? "Available" : "NULL");
 ESP_LOGI(TAG, "🎯 Starting detailed system monitoring loop...");
 
 uint32_t monitor_count = 0;
 uint32_t last_status_log = 0;
 
 while (true) {
     monitor_count++;
     uint32_t current_time = esp_timer_get_time() / 1000;
     
     // Log every 5 seconds with detailed system status
     if (current_time - last_status_log > 5000) {
         ESP_LOGI(TAG, "📋 === SYSTEM STATUS REPORT #%lu ===", monitor_count);
         ESP_LOGI(TAG, "⏰ Uptime: %lu.%03lu seconds", current_time / 1000, current_time % 1000);
         ESP_LOGI(TAG, "💾 Free heap: Internal=%lu KB, PSRAM=%lu KB", 
                  esp_get_free_heap_size() / 1024,
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
         
         // Check task system status
         if (task_sys) {
             task_state_t cellular_state = task_sys->get_task_state("cellular");
             task_state_t gps_state = task_sys->get_task_state("gps"); 
             task_state_t battery_state = task_sys->get_task_state("battery");
             task_state_t monitor_state = task_sys->get_task_state("monitor");
             
             ESP_LOGI(TAG, "📡 Cellular Task: State=%d", cellular_state);
             ESP_LOGI(TAG, "🛰️  GPS Task: State=%d", gps_state);
             ESP_LOGI(TAG, "🔋 Battery Task: State=%d", battery_state);
             ESP_LOGI(TAG, "🔍 Monitor Task: State=%d", monitor_state);
         }
         
         // Check multitask manager status
         ESP_LOGI(TAG, "🚀 Multitask Manager: Active background jobs processing");
         
         // Reset for next interval
         last_status_log = current_time;
     }
     
     // Brief delay to prevent flooding
     vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
 }
 if (task_sys->wait_for_system_ready(120000)) { // 2 minute timeout
     ESP_LOGI(TAG, "🎯 System fully operational: Cellular + GPS + MQTT ready!");
 } else {
     ESP_LOGW(TAG, "⚠️  System not fully ready within timeout, but continuing...");
 }
 
 // Main supervision loop - minimal monitoring
 uint32_t status_counter = 0;
 const uint32_t STATUS_INTERVAL = 60; // Print status every 60 cycles (5 minutes)
 
 while (1) {
     // Lightweight system monitoring
     if (status_counter % STATUS_INTERVAL == 0) {
         ESP_LOGI(TAG, "📊 === SYSTEM STATUS (Runtime: %d min) ===", status_counter / 12);
         
         if (task_sys->is_system_healthy()) {
             ESP_LOGI(TAG, "✅ System health: GOOD");
         } else {
             ESP_LOGW(TAG, "⚠️  System health: DEGRADED");
             task_sys->print_system_status();
         }
         
         // Print stack usage for monitoring
         ESP_LOGI(TAG, "📈 Stack Usage (bytes free):");
         ESP_LOGI(TAG, "   📡 Cellular: %lu", task_sys->get_stack_usage("cellular"));
         ESP_LOGI(TAG, "   🛰️  GPS: %lu", task_sys->get_stack_usage("gps"));
         ESP_LOGI(TAG, "   📨 MQTT: %lu", task_sys->get_stack_usage("mqtt"));
         ESP_LOGI(TAG, "   🔋 Battery: %lu", task_sys->get_stack_usage("battery"));
         ESP_LOGI(TAG, "   🔍 Monitor: %lu", task_sys->get_stack_usage("monitor"));
         ESP_LOGI(TAG, "==========================================");
     }
     
     status_counter++;
     vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second intervals - very lightweight
 }
}

// Old initialize_modules function - replaced by task system
// Commenting out entire function to allow build to succeed
/*
static bool initialize_modules(void)
{
 ESP_LOGI(TAG, "🚀 === SEQUENTIAL STARTUP SYSTEM (Connection Manager) ===");
 
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
 
 // Initialize LTE module for UART communication (but don't connect yet)
 ESP_LOGI(TAG, "📱 Initializing LTE module for UART communication...");
 if (!lte_if->init(&system_config.lte)) {
 ESP_LOGE(TAG, "Failed to initialize LTE module");
 return false;
 }
 ESP_LOGI(TAG, "✅ LTE module initialized");
 
 // Initialize GPS module interface (but don't start GPS yet)
 ESP_LOGI(TAG, "� Initializing GPS module interface...");
 if (!gps_if->init(&system_config.gps)) {
 ESP_LOGW(TAG, "GPS module interface initialization failed");
 // Don't fail completely - GPS may still work
 }
 ESP_LOGI(TAG, "✅ GPS module interface initialized");
 
 // Initialize MQTT module interface (but don't connect yet)
 ESP_LOGI(TAG, "📨 Initializing MQTT module interface...");
 if (!mqtt_if->init(&system_config.mqtt)) {
 ESP_LOGE(TAG, "Failed to initialize MQTT module interface");
 return false;
 }
 ESP_LOGI(TAG, "✅ MQTT module interface initialized");
 
 // *** NOW USE CONNECTION MANAGER FOR SEQUENTIAL STARTUP ***
 ESP_LOGI(TAG, "🔧 Starting Connection Manager Sequential Startup System");
 
 // Get connection manager interface
 const connection_manager_interface_t* conn_mgr = connection_manager_get_interface();
 if (!conn_mgr) {
 ESP_LOGE(TAG, "Connection manager interface not available");
 return false;
 }
 
 // Initialize connection manager with recovery configuration
 recovery_config_t recovery_config = RECOVERY_CONFIG_DEFAULT;
 recovery_config.debug_enabled = true; // Enable debug for startup
 
 if (!conn_mgr->init(&recovery_config)) {
 ESP_LOGE(TAG, "Failed to initialize connection manager");
 return false;
 }
 ESP_LOGI(TAG, "✅ Connection manager initialized");
 
 // Execute sequential startup: Cellular -> GPS -> MQTT
 ESP_LOGI(TAG, "🚀 Starting full system with sequential startup...");
 if (!conn_mgr->startup_full_system()) {
 ESP_LOGE(TAG, "❌ Sequential startup failed");
 ESP_LOGE(TAG, "Check cellular connection, antenna, and SIM card");
 return false;
 }
 
 ESP_LOGI(TAG, "🎉 === SEQUENTIAL STARTUP COMPLETED SUCCESSFULLY ===");
 ESP_LOGI(TAG, "✅ All systems operational with connection monitoring active");
 ESP_LOGI(TAG, "⚡ CPU OPTIMIZED: Cellular/GPS initialized ONCE - will only parse data from now on");
 return true;
}
*/

// GPS polling timer callback - SAFE: Only sets flag, no complex operations
static void gps_polling_timer_callback(TimerHandle_t xTimer)
{
 // SAFE: Only set a flag - absolutely NO logging, function calls, or complex operations!
 gps_polling_requested = true;
}

// MQTT transmission timer callback - SAFE: Only sets flag, no complex operations 
static void transmission_timer_callback(TimerHandle_t xTimer)
{
 // SAFE: Only set a flag - absolutely NO logging, function calls, or complex operations!
 mqtt_transmission_requested = true;
}

// Note: Using mqtt_create_json_payload() instead of local implementation

// Old data_aggregation_task - replaced by individual tasks in task system
/*
 * @brief Data aggregation task - collects data from queues and publishes via MQTT
 * Runs on Core 0 (Protocol Core) alongside MQTT task
 *
void data_aggregation_task(void* params)
{
 const task_manager_t* task_mgr = (const task_manager_t*)params;
 ESP_LOGI(TAG, " [Core 0] Data Aggregation Task started");
 
 // Register this task with the watchdog
 esp_err_t err = esp_task_wdt_add(NULL);
 bool watchdog_registered = (err == ESP_OK);
 if (!watchdog_registered) {
 ESP_LOGW(TAG, "Failed to add data aggregation task to watchdog: %s", esp_err_to_name(err));
 }
 
 gps_data_t gps_data = {0};
 battery_data_t battery_data = {0};
 bool have_gps = false;
 bool have_battery = false;
 
 uint32_t last_publish_time = 0;
 const uint32_t publish_interval_ms = system_config.system.transmission_interval_ms;
 
 while (task_mgr->tasks_running) {
 // PRIORITY 1: Handle timer requests FIRST
 
 // Handle GPS polling timer request (SAFE: Called from task, not timer)
 if (gps_polling_requested) {
 gps_polling_requested = false; // Clear flag first
 ESP_LOGI(TAG, "🕐 GPS polling timer triggered");
 if (collect_and_parse_gps_data()) {
 ESP_LOGI(TAG, " Fresh GPS data collected by timer");
 } else {
 ESP_LOGW(TAG, " GPS data collection failed on timer trigger");
 }
 }
 
 // Handle MQTT transmission timer request (SAFE: Called from task, not timer)
 if (mqtt_transmission_requested) {
 mqtt_transmission_requested = false; // Clear flag first
 ESP_LOGI(TAG, "🕐 MQTT transmission timer triggered");
 if (transmit_gps_data_via_mqtt()) {
 ESP_LOGI(TAG, " MQTT transmission completed by timer");
 } else {
 ESP_LOGW(TAG, " MQTT transmission failed on timer trigger");
 }
 }
 
 // PRIORITY 2: Normal queue processing
 
 // Collect GPS data (non-blocking)
 if (xQueueReceive(task_mgr->gps_data_queue, &gps_data, pdMS_TO_TICKS(100)) == pdTRUE) {
 have_gps = true;
 ESP_LOGD(TAG, " Received GPS data: %.6f,%.6f (%d sats)", 
 gps_data.latitude, gps_data.longitude, gps_data.satellites);
 }
 
 // Collect battery data (non-blocking)
 if (xQueueReceive(task_mgr->battery_data_queue, &battery_data, pdMS_TO_TICKS(10)) == pdTRUE) {
 have_battery = true;
 ESP_LOGD(TAG, " Received battery data: %.1f%% (%.2fV)", 
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
 ESP_LOGW(TAG, " Failed to publish data");
 }
 free(json_string);
 }
 cJSON_Delete(root);
 }
 
 // Feed watchdog and yield only if successfully registered
 if (watchdog_registered) {
 esp_task_wdt_reset();
 }
 vTaskDelay(pdMS_TO_TICKS(500)); // Check for data every 500ms
 }
 
 ESP_LOGI(TAG, "🛑 [Core 0] Data Aggregation Task stopped");
 
 // Unregister from watchdog before deletion only if registered
 if (watchdog_registered) {
 esp_task_wdt_delete(NULL);
 }
 vTaskDelete(NULL);
}
*/

// GPS data collection and parsing function  
// SAFE FUNCTION: Called from tasks, NOT from timer callbacks
// CPU OPTIMIZED: Only reads/parses GPS data - NO re-initialization 
// GPS is initialized ONCE during startup and then we only parse NMEA data
static bool collect_and_parse_gps_data(void)
{
 if (!gps_if) {
 ESP_LOGE(TAG, "GPS interface not available");
 return false;
 }
 
 // Flush old buffer data to prepare for fresh GPS poll
 gps_buffer_flush();
 
 // CPU EFFICIENT: Only read/parse existing NMEA data - no GPS restart
 gps_data_t gps_data = {0};
 if (!gps_if->read_data(&gps_data)) {
 ESP_LOGW(TAG, "Failed to read GPS data from module");
 return false;
 }
 
 // Read battery data
 battery_data_t battery_data = {0};
 if (battery_if && battery_if->read_data(&battery_data)) {
 memcpy(&last_battery_data, &battery_data, sizeof(battery_data_t));
 } else {
 // Use last known battery data if read fails
 memcpy(&battery_data, &last_battery_data, sizeof(battery_data_t));
 }
 
 // Store fresh data in buffer (solves MQTT NMEA interference)
 if (gps_buffer_store(&gps_data, &battery_data)) {
 ESP_LOGI(TAG, " GPS data stored in buffer: Lat=%.6f, Lon=%.6f, Alt=%.1fm, Sats=%d, HDOP=%.2f, Fix=%s", 
         gps_data.latitude, gps_data.longitude, gps_data.altitude,
         gps_data.satellites, gps_data.hdop, gps_data.fix_valid ? "VALID" : "INVALID");
 
 // Update last known GPS data for fallback
 memcpy(&last_gps_data, &gps_data, sizeof(gps_data_t));
 fresh_gps_data_available = true;
 return true;
 } else {
 ESP_LOGE(TAG, "Failed to store GPS data in buffer");
 return false;
 }
}

// SAFE FUNCTION: Handles MQTT transmission from task context (NOT timer callback)
// NEW: Reads from GPS buffer to avoid NMEA interference during MQTT operations
static bool transmit_gps_data_via_mqtt(void)
{
 ESP_LOGI(TAG, " Starting MQTT data transmission...");
 
 if (!mqtt_if) {
 ESP_LOGE(TAG, " MQTT interface not available");
 return false;
 }
 
 // Read GPS and battery data from buffer (no NMEA interference!)
 gps_data_t gps_data = {0};
 battery_data_t battery_data = {0};
 bool has_fresh_data = false;
 
 if (gps_buffer_has_data()) {
 if (gps_buffer_read(&gps_data, &battery_data)) {
 has_fresh_data = true;
 ESP_LOGI(TAG, " Using fresh GPS data from buffer");
 } else {
 ESP_LOGW(TAG, " Failed to read from GPS buffer - using fallback data");
 }
 } else {
 ESP_LOGW(TAG, " No data in GPS buffer - using last known position");
 }
 
 // Fallback to last known data if buffer is empty
 if (!has_fresh_data) {
 memcpy(&gps_data, &last_gps_data, sizeof(gps_data_t));
 memcpy(&battery_data, &last_battery_data, sizeof(battery_data_t));
 }
 
 ESP_LOGI(TAG, " GPS Data: Lat=%.6f, Lon=%.6f, Sats=%d, Fix=%s", 
 gps_data.latitude, gps_data.longitude, gps_data.satellites,
 gps_data.fix_valid ? "VALID" : "INVALID");
 
 // Check MQTT connection status
 mqtt_status_t status = mqtt_if->get_status();
 if (status != MQTT_STATUS_CONNECTED) {
 ESP_LOGW(TAG, " MQTT not connected, attempting reconnection...");
 if (!mqtt_if->connect()) {
 ESP_LOGE(TAG, " MQTT reconnection failed");
 return false;
 }
 ESP_LOGI(TAG, " MQTT reconnection successful");
 }
 
 // Create enhanced JSON payload
 char json_buffer[1024];
 if (!mqtt_create_enhanced_json_payload(&gps_data, &battery_data, 
 has_fresh_data, 
 json_buffer, sizeof(json_buffer))) {
 ESP_LOGE(TAG, " Failed to create enhanced JSON payload");
 return false;
 }
 
 ESP_LOGI(TAG, "📦 Publishing to topic: %s", system_config.mqtt.topic);
 
 // Publish data
 mqtt_publish_result_t result = {0};
 if (mqtt_if->publish_json(system_config.mqtt.topic, json_buffer, &result)) {
 ESP_LOGI(TAG, " Data transmitted successfully to %s:%d", 
 system_config.mqtt.broker_host, system_config.mqtt.broker_port);
 ESP_LOGI(TAG, " GPS: %.6f,%.6f | Battery: %.1fV (%.0f%%) | Satellites: %d", 
 gps_data.latitude, gps_data.longitude, battery_data.voltage, 
 battery_data.percentage, gps_data.satellites);
 
 // Mark GPS data as used (for next cycle) - only if we used fresh data
 if (has_fresh_data) {
 fresh_gps_data_available = false;
 ESP_LOGI(TAG, " Fresh GPS data consumed, ready for next poll cycle");
 }
 return true;
 } else {
 ESP_LOGW(TAG, " Failed to transmit data via MQTT");
 return false;
 }
}