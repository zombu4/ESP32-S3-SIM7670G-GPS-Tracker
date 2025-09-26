/**
 * @file task_manager.c
 * @brief Dual-core task management implementation for ESP32-S3
 */

#include "task_manager.h"
#include "modules/lte/lte_module.h"
#include "modules/gps/gps_module.h" 
#include "modules/mqtt/mqtt_module.h"
#include "modules/battery/battery_module.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <string.h>

static const char* TAG = "TASK_MANAGER";

// Global task manager instance
static task_manager_t g_task_manager = {0};

// Forward declarations
static bool init_impl(void);
static bool start_all_tasks_impl(void);
static void stop_all_tasks_impl(void);
static bool send_gps_data_impl(gps_data_t* data);
static bool send_battery_data_impl(battery_data_t* data);
static bool publish_mqtt_impl(const char* topic, const char* payload, int qos);
static bool send_at_command_impl(const char* command, char* response, uint32_t timeout_ms);
static void feed_watchdog_impl(void);

/**
 * @brief Initialize task manager
 */
static bool init_impl(void)
{
    ESP_LOGI(TAG, "🚀 Initializing dual-core task manager...");
    
    // Create inter-core communication queues
    g_task_manager.gps_data_queue = xQueueCreate(GPS_DATA_QUEUE_SIZE, sizeof(gps_data_t));
    g_task_manager.battery_data_queue = xQueueCreate(BATTERY_DATA_QUEUE_SIZE, sizeof(battery_data_t));
    g_task_manager.mqtt_publish_queue = xQueueCreate(MQTT_PUBLISH_QUEUE_SIZE, sizeof(mqtt_publish_msg_t));
    g_task_manager.at_command_queue = xQueueCreate(AT_COMMAND_QUEUE_SIZE, sizeof(at_command_msg_t));
    
    if (!g_task_manager.gps_data_queue || !g_task_manager.battery_data_queue || 
        !g_task_manager.mqtt_publish_queue || !g_task_manager.at_command_queue) {
        ESP_LOGE(TAG, "❌ Failed to create communication queues");
        return false;
    }
    
    // Create shared UART mutex for AT command synchronization
    g_task_manager.shared_uart_mutex = xSemaphoreCreateMutex();
    if (!g_task_manager.shared_uart_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create UART mutex");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Task manager initialized");
    ESP_LOGI(TAG, "📊 Queue sizes - GPS:%d, Battery:%d, MQTT:%d, AT:%d", 
             GPS_DATA_QUEUE_SIZE, BATTERY_DATA_QUEUE_SIZE, 
             MQTT_PUBLISH_QUEUE_SIZE, AT_COMMAND_QUEUE_SIZE);
    
    return true;
}

/**
 * @brief Start all tasks on appropriate cores
 */
static bool start_all_tasks_impl(void)
{
    ESP_LOGI(TAG, "🎯 Starting tasks on dual cores...");
    
    // === CORE 0 TASKS (Protocol Core) ===
    ESP_LOGI(TAG, "🔧 Starting Core 0 tasks (Protocol)...");
    
    // LTE Management Task - Core 0, High Priority
    BaseType_t result = xTaskCreatePinnedToCore(
        lte_management_task,
        "lte_mgmt",
        STACK_SIZE_LARGE,
        NULL,
        PRIORITY_HIGH,
        &g_task_manager.lte_task,
        PROTOCOL_CORE
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create LTE management task");
        return false;
    }
    
    // MQTT Communication Task - Core 0, Normal Priority
    result = xTaskCreatePinnedToCore(
        mqtt_communication_task,
        "mqtt_comm", 
        STACK_SIZE_MEDIUM,
        NULL,
        PRIORITY_NORMAL,
        &g_task_manager.mqtt_task,
        PROTOCOL_CORE
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create MQTT communication task");
        return false;
    }
    
    // === CORE 1 TASKS (Application Core) ===
    ESP_LOGI(TAG, "📱 Starting Core 1 tasks (Application)...");
    
    // GPS Data Collection Task - Core 1, High Priority
    result = xTaskCreatePinnedToCore(
        gps_data_collection_task,
        "gps_collect",
        STACK_SIZE_MEDIUM,
        NULL,
        PRIORITY_HIGH,
        &g_task_manager.gps_task,
        APPLICATION_CORE
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create GPS data collection task");
        return false;
    }
    
    // Battery Monitoring Task - Core 1, Low Priority
    result = xTaskCreatePinnedToCore(
        battery_monitoring_task,
        "battery_mon",
        STACK_SIZE_SMALL,
        NULL,
        PRIORITY_LOW,
        &g_task_manager.battery_task,
        APPLICATION_CORE
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create battery monitoring task");
        return false;
    }
    
    // System Watchdog Task - Core 0, Normal Priority
    result = xTaskCreatePinnedToCore(
        system_watchdog_task,
        "watchdog",
        STACK_SIZE_SMALL,
        NULL,
        PRIORITY_NORMAL,
        &g_task_manager.watchdog_task,
        PROTOCOL_CORE
    );
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create watchdog task");
        return false;
    }
    
    g_task_manager.tasks_running = true;
    
    ESP_LOGI(TAG, "✅ All tasks started successfully");
    ESP_LOGI(TAG, "🏗️  Architecture: Core 0 (Protocol) | Core 1 (Application)");
    
    return true;
}

/**
 * @brief Stop all tasks
 */
static void stop_all_tasks_impl(void)
{
    ESP_LOGI(TAG, "🛑 Stopping all tasks...");
    
    g_task_manager.tasks_running = false;
    
    if (g_task_manager.lte_task) {
        vTaskDelete(g_task_manager.lte_task);
        g_task_manager.lte_task = NULL;
    }
    
    if (g_task_manager.mqtt_task) {
        vTaskDelete(g_task_manager.mqtt_task);
        g_task_manager.mqtt_task = NULL;
    }
    
    if (g_task_manager.gps_task) {
        vTaskDelete(g_task_manager.gps_task);
        g_task_manager.gps_task = NULL;
    }
    
    if (g_task_manager.battery_task) {
        vTaskDelete(g_task_manager.battery_task);
        g_task_manager.battery_task = NULL;
    }
    
    if (g_task_manager.watchdog_task) {
        vTaskDelete(g_task_manager.watchdog_task);
        g_task_manager.watchdog_task = NULL;
    }
    
    ESP_LOGI(TAG, "✅ All tasks stopped");
}

/**
 * @brief Send GPS data between cores
 */
static bool send_gps_data_impl(gps_data_t* data)
{
    if (!data || !g_task_manager.gps_data_queue) {
        return false;
    }
    
    return xQueueSend(g_task_manager.gps_data_queue, data, pdMS_TO_TICKS(100)) == pdTRUE;
}

/**
 * @brief Send battery data between cores
 */
static bool send_battery_data_impl(battery_data_t* data)
{
    if (!data || !g_task_manager.battery_data_queue) {
        return false;
    }
    
    return xQueueSend(g_task_manager.battery_data_queue, data, pdMS_TO_TICKS(100)) == pdTRUE;
}

/**
 * @brief Queue MQTT publish message
 */
static bool publish_mqtt_impl(const char* topic, const char* payload, int qos)
{
    if (!topic || !payload || !g_task_manager.mqtt_publish_queue) {
        return false;
    }
    
    mqtt_publish_msg_t msg = {0};
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.qos = qos;
    msg.retain = false;
    
    return xQueueSend(g_task_manager.mqtt_publish_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE;
}

/**
 * @brief Send AT command with synchronization
 */
static bool send_at_command_impl(const char* command, char* response, uint32_t timeout_ms)
{
    if (!command || !response || !g_task_manager.at_command_queue) {
        return false;
    }
    
    at_command_msg_t msg = {0};
    strncpy(msg.command, command, sizeof(msg.command) - 1);
    msg.timeout_ms = timeout_ms;
    msg.completion_sem = xSemaphoreCreateBinary();
    
    if (!msg.completion_sem) {
        return false;
    }
    
    // Send command to queue
    if (xQueueSend(g_task_manager.at_command_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(msg.completion_sem);
        return false;
    }
    
    // Wait for completion
    bool success = false;
    if (xSemaphoreTake(msg.completion_sem, pdMS_TO_TICKS(timeout_ms + 1000)) == pdTRUE) {
        success = msg.success;
        if (success) {
            strncpy(response, msg.response, 511);
            response[511] = '\0';
        }
    }
    
    vSemaphoreDelete(msg.completion_sem);
    return success;
}

/**
 * @brief Feed watchdog (non-blocking)
 */
static void feed_watchdog_impl(void)
{
    // Only feed if current task is registered with watchdog
    // This prevents "task not found" errors from non-task contexts
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    if (current_task != NULL) {
        // Only feed watchdog if we're in a task context that's likely registered
        const char* task_name = pcTaskGetName(current_task);
        
        // Only feed if task name contains known patterns indicating watchdog registration
        if (task_name && (strstr(task_name, "lte") || strstr(task_name, "gps") || strstr(task_name, "mqtt") || 
            strstr(task_name, "battery") || strstr(task_name, "watchdog") || strstr(task_name, "data"))) {
            esp_task_wdt_reset();
        }
    }
}

// ============================================================================
// CORE 0 TASKS (Protocol Core)
// ============================================================================

/**
 * @brief LTE management task running on Core 0
 */
void lte_management_task(void* params)
{
    ESP_LOGI(TAG, "🔧 [Core 0] LTE Management Task started");
    
    // Register this task with the watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add LTE task to watchdog: %s", esp_err_to_name(err));
    }
    
    const lte_interface_t* lte = lte_get_interface();
    at_command_msg_t at_msg;
    TickType_t last_status_check = 0;
    const TickType_t STATUS_CHECK_INTERVAL = pdMS_TO_TICKS(30000); // Check LTE status every 30 seconds
    
    while (g_task_manager.tasks_running) {
        // Process queued AT commands
        if (xQueueReceive(g_task_manager.at_command_queue, &at_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Take UART mutex for exclusive access
            if (xSemaphoreTake(g_task_manager.shared_uart_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                
                at_response_t response = {0};
                at_msg.success = lte && lte->send_at_command(at_msg.command, &response, at_msg.timeout_ms);
                
                if (at_msg.success) {
                    strncpy(at_msg.response, response.response, sizeof(at_msg.response) - 1);
                }
                
                xSemaphoreGive(g_task_manager.shared_uart_mutex);
                
                // Signal completion
                if (at_msg.completion_sem) {
                    xSemaphoreGive(at_msg.completion_sem);
                }
            }
        }
        
        // Periodic LTE status polling (every 30 seconds)
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_status_check) >= STATUS_CHECK_INTERVAL) {
            if (xSemaphoreTake(g_task_manager.shared_uart_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                
                // Poll LTE network status - NO auto operations
                if (lte) {
                    lte_status_t status = lte->get_connection_status();
                    ESP_LOGD(TAG, "🔧 LTE status poll: %s", 
                            (status == LTE_STATUS_CONNECTED) ? "CONNECTED" : "DISCONNECTED");
                }
                
                xSemaphoreGive(g_task_manager.shared_uart_mutex);
                last_status_check = current_time;
            }
        }
        
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Small delay to prevent task hogging
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "🛑 [Core 0] LTE Management Task stopped");
    
    // Unregister from watchdog before deletion
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

/**
 * @brief MQTT communication task running on Core 0
 */
void mqtt_communication_task(void* params)
{
    ESP_LOGI(TAG, "💬 [Core 0] MQTT Communication Task started");
    
    // Register this task with the watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add MQTT task to watchdog: %s", esp_err_to_name(err));
    }
    
    const mqtt_interface_t* mqtt = mqtt_get_interface();
    mqtt_publish_msg_t publish_msg;
    TickType_t last_mqtt_status_check = 0;
    const TickType_t MQTT_STATUS_CHECK_INTERVAL = pdMS_TO_TICKS(45000); // Check MQTT status every 45 seconds
    
    // Initialize MQTT
    if (mqtt && !mqtt->init(NULL)) {
        ESP_LOGE(TAG, "❌ [Core 0] MQTT initialization failed");
    }
    
    while (g_task_manager.tasks_running) {
        // Process publish queue
        if (xQueueReceive(g_task_manager.mqtt_publish_queue, &publish_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (mqtt) {
                // Create MQTT message structure
                mqtt_message_t mqtt_msg;
                strncpy(mqtt_msg.topic, publish_msg.topic, sizeof(mqtt_msg.topic) - 1);
                mqtt_msg.topic[sizeof(mqtt_msg.topic) - 1] = '\0';
                strncpy(mqtt_msg.payload, publish_msg.payload, sizeof(mqtt_msg.payload) - 1);
                mqtt_msg.payload[sizeof(mqtt_msg.payload) - 1] = '\0';
                mqtt_msg.qos = publish_msg.qos;
                mqtt_msg.retain = publish_msg.retain;
                mqtt_msg.timestamp = esp_log_timestamp();
                
                mqtt_publish_result_t result;
                mqtt->publish(&mqtt_msg, &result);
            }
        }
        
        // Periodic MQTT status polling (every 45 seconds)
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_mqtt_status_check) >= MQTT_STATUS_CHECK_INTERVAL) {
            // Poll MQTT connection status - NO auto operations
            if (mqtt) {
                mqtt_status_t status = mqtt->get_status();
                ESP_LOGD(TAG, "💬 MQTT status poll: %s", 
                        (status == MQTT_STATUS_CONNECTED) ? "CONNECTED" : "DISCONNECTED");
            }
            last_mqtt_status_check = current_time;
        }
        
        // Feed watchdog
        esp_task_wdt_reset();
        
        // Small delay
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "🛑 [Core 0] MQTT Communication Task stopped");
    
    // Unregister from watchdog before deletion
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

/**
 * @brief System watchdog task running on Core 0
 */
void system_watchdog_task(void* params)
{
    ESP_LOGI(TAG, "🐕 [Core 0] System Watchdog Task started");
    
    // Register this task with the watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add watchdog task to watchdog: %s", esp_err_to_name(err));
    }
    
    while (g_task_manager.tasks_running) {
        // Feed main watchdog
        esp_task_wdt_reset();
        
        // System health checks could go here
        
        // Watchdog interval
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "🛑 [Core 0] System Watchdog Task stopped");
    
    // Unregister from watchdog before deletion
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

// ============================================================================
// CORE 1 TASKS (Application Core)
// ============================================================================

/**
 * @brief GPS data collection task running on Core 1
 */
void gps_data_collection_task(void* params)
{
    ESP_LOGI(TAG, "🛰️  [Core 1] GPS Data Collection Task started");
    
    // Register this task with the watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add GPS task to watchdog: %s", esp_err_to_name(err));
    }
    
    const gps_interface_t* gps = gps_get_interface();
    gps_data_t gps_data;
    
    while (g_task_manager.tasks_running) {
        // CRITICAL: Take UART mutex before GPS polling to prevent AT command collisions
        if (xSemaphoreTake(g_task_manager.shared_uart_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            // Collect GPS data via polling (AT+CGNSINF) - NO auto output
            if (gps && gps->read_data(&gps_data)) {
                // Data is read successfully, send to queue
                ESP_LOGD(TAG, "📍 GPS data polled successfully (15s interval)");
                
                // Send to other core via queue
                send_gps_data_impl(&gps_data);
            } else {
                ESP_LOGD(TAG, "📍 GPS polling - no valid data this cycle");
            }
            
            // Release UART mutex
            xSemaphoreGive(g_task_manager.shared_uart_mutex);
            
        } else {
            ESP_LOGW(TAG, "⚠️  GPS polling skipped - UART busy with AT commands");
        }
        
        // Feed watchdog
        esp_task_wdt_reset();
        
        // GPS polling interval - 15 seconds as requested
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
    
    ESP_LOGI(TAG, "🛑 [Core 1] GPS Data Collection Task stopped");
    
    // Unregister from watchdog before deletion
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

/**
 * @brief Battery monitoring task running on Core 1
 */
void battery_monitoring_task(void* params)
{
    ESP_LOGI(TAG, "🔋 [Core 1] Battery Monitoring Task started");
    
    // Register this task with the watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add battery task to watchdog: %s", esp_err_to_name(err));
    }
    
    const battery_interface_t* battery = battery_get_interface();
    battery_data_t battery_data;
    
    while (g_task_manager.tasks_running) {
        // Read battery data (non-blocking)
        if (battery) {
            // Use the battery interface's read_data method
            if (battery->read_data(&battery_data)) {
                // Data read successfully
                // Send to other core via queue
                send_battery_data_impl(&battery_data);
            }
        }
        
        // Feed watchdog  
        esp_task_wdt_reset();
        
        // Battery update interval (longer than GPS)
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    
    ESP_LOGI(TAG, "🛑 [Core 1] Battery Monitoring Task stopped");
    
    // Unregister from watchdog before deletion
    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

// ============================================================================
// Interface Implementation
// ============================================================================

const task_manager_t* task_manager_get_interface(void)
{
    static bool initialized = false;
    
    if (!initialized) {
        g_task_manager.init = init_impl;
        g_task_manager.start_all_tasks = start_all_tasks_impl;
        g_task_manager.stop_all_tasks = stop_all_tasks_impl;
        g_task_manager.send_gps_data = send_gps_data_impl;
        g_task_manager.send_battery_data = send_battery_data_impl;
        g_task_manager.publish_mqtt = publish_mqtt_impl;
        g_task_manager.send_at_command = send_at_command_impl;
        g_task_manager.feed_watchdog = feed_watchdog_impl;
        
        initialized = true;
    }
    
    return &g_task_manager;
}