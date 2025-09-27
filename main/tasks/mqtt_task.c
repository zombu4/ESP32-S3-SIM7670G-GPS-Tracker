#include "task_system.h"
#include "modules/mqtt/mqtt_module.h"
#include "esp_task_wdt.h"
#include "cJSON.h"

// External reference to system config
extern tracker_system_config_t system_config;

static const char *TAG = "MQTT_TASK";

void mqtt_task_entry(void* parameters) {
    task_system_t* sys = (task_system_t*)parameters;
    
    ESP_LOGI(TAG, "📨 MQTT Task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "🎯 Prerequisites met: Cellular=READY, GPS=FIX_ACQUIRED");
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    
    sys->mqtt_task.state = TASK_STATE_RUNNING;
    sys->mqtt_task.current_cpu = xPortGetCoreID();
    
    // Get MQTT module interface
    const mqtt_interface_t* mqtt_if = mqtt_get_interface();
    
    bool mqtt_initialized = false;
    bool mqtt_connected = false;
    uint32_t init_retry_count = 0;
    const uint32_t MAX_INIT_RETRIES = 5;
    const uint32_t PUBLISH_INTERVAL = 30000; // 30 seconds
    const uint32_t CONNECTION_CHECK_INTERVAL = 10000; // 10 seconds
    uint32_t last_publish = 0;
    uint32_t last_connection_check = 0;
    
    while (sys->system_running) {
        esp_task_wdt_reset();
        update_task_heartbeat("mqtt");
        
        uint32_t current_time = get_current_timestamp_ms();
        
        // Verify prerequisites are still met
        // MQTT only requires cellular connection, GPS fix is optional for basic operation
        EventBits_t required_bits = EVENT_CELLULAR_READY;
        EventBits_t current_bits = xEventGroupWaitBits(
            sys->system_events,
            required_bits,
            pdFALSE,  // Don't clear
            pdTRUE,   // Wait for all bits
            0         // No wait
        );
        
        if ((current_bits & required_bits) != required_bits) {
            ESP_LOGW(TAG, "⚠️  Prerequisites lost, pausing MQTT operations");
            if (mqtt_connected) {
                mqtt_connected = false;
                xEventGroupClearBits(sys->system_events, EVENT_MQTT_READY);
            }
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before checking again
            continue;
        }
        
        // Handle initialization phase
        if (!mqtt_initialized) {
            ESP_LOGI(TAG, "🔧 Initializing MQTT module (attempt %lu/%d)", 
                     init_retry_count + 1, MAX_INIT_RETRIES);
            
            if (mqtt_if && mqtt_if->init) {
                if (mqtt_if->init(&system_config.mqtt)) {
                    ESP_LOGI(TAG, "✅ MQTT module initialized successfully");
                    mqtt_initialized = true;
                    init_retry_count = 0;
                    
                    // Try to connect immediately
                    if (mqtt_if->connect && mqtt_if->connect()) {
                        mqtt_connected = true;
                        xEventGroupSetBits(sys->system_events, EVENT_MQTT_READY);
                        ESP_LOGI(TAG, "🌐 MQTT connected to broker");
                        sys->mqtt_task.state = TASK_STATE_READY;
                    }
                } else {
                    init_retry_count++;
                    ESP_LOGW(TAG, "⚠️  MQTT initialization failed, retry %lu/%d", 
                             init_retry_count, MAX_INIT_RETRIES);
                    
                    if (init_retry_count >= MAX_INIT_RETRIES) {
                        ESP_LOGE(TAG, "❌ MQTT initialization failed after %d retries", MAX_INIT_RETRIES);
                        sys->mqtt_task.state = TASK_STATE_ERROR;
                        sys->mqtt_error_count++;
                        init_retry_count = 0; // Reset for next attempt cycle
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
                }
            }
        }
        
        // Handle connection monitoring
        else if (mqtt_initialized && (current_time - last_connection_check) >= CONNECTION_CHECK_INTERVAL) {
            last_connection_check = current_time;
            
            if (mqtt_if && mqtt_if->is_connected) {
                bool currently_connected = mqtt_if->is_connected();
                
                if (currently_connected && !mqtt_connected) {
                    // Connection restored
                    mqtt_connected = true;
                    xEventGroupSetBits(sys->system_events, EVENT_MQTT_READY);
                    xEventGroupClearBits(sys->system_events, EVENT_MQTT_DISCONNECTED);
                    ESP_LOGI(TAG, "🔄 MQTT connection restored");
                    sys->mqtt_task.state = TASK_STATE_READY;
                    
                } else if (!currently_connected && mqtt_connected) {
                    // Connection lost
                    mqtt_connected = false;
                    xEventGroupClearBits(sys->system_events, EVENT_MQTT_READY);
                    xEventGroupSetBits(sys->system_events, EVENT_MQTT_DISCONNECTED);
                    ESP_LOGW(TAG, "⚠️  MQTT connection lost");
                    sys->mqtt_task.state = TASK_STATE_ERROR;
                    sys->mqtt_error_count++;
                    
                    // Attempt to reconnect
                    if (mqtt_if->connect) {
                        ESP_LOGI(TAG, "🔄 Attempting to reconnect MQTT...");
                        if (mqtt_if->connect()) {
                            mqtt_connected = true;
                            xEventGroupSetBits(sys->system_events, EVENT_MQTT_READY);
                            ESP_LOGI(TAG, "✅ MQTT reconnected successfully");
                        }
                    }
                }
            }
        }
        
        // Handle periodic data publishing
        if (mqtt_connected && (current_time - last_publish) >= PUBLISH_INTERVAL) {
            last_publish = current_time;
            
            // Check if fresh GPS data is available
            EventBits_t gps_data_bits = xEventGroupWaitBits(
                sys->system_events,
                EVENT_GPS_DATA_FRESH,
                pdTRUE,   // Clear the bit after reading
                pdTRUE,   // Wait for all bits
                0         // No wait
            );
            
            if (gps_data_bits & EVENT_GPS_DATA_FRESH) {
                ESP_LOGI(TAG, "📡 Publishing fresh GPS data to MQTT broker");
                
                // TODO: Get actual GPS and battery data from shared memory
                // For now, create a test payload
                cJSON *json = cJSON_CreateObject();
                if (json) {
                    cJSON *timestamp = cJSON_CreateNumber(current_time);
                    cJSON *device_id = cJSON_CreateString("ESP32GPS_TEST");
                    cJSON *status = cJSON_CreateString("online");
                    
                    cJSON_AddItemToObject(json, "timestamp", timestamp);
                    cJSON_AddItemToObject(json, "device_id", device_id);
                    cJSON_AddItemToObject(json, "status", status);
                    
                    char *json_string = cJSON_Print(json);
                    if (json_string && mqtt_if && mqtt_if->publish_json) {
                        mqtt_publish_result_t result = {0};
                        if (mqtt_if->publish_json("gps_tracker/data", json_string, &result)) {
                            ESP_LOGI(TAG, "📤 MQTT publish successful");
                        } else {
                            ESP_LOGW(TAG, "⚠️  MQTT publish failed");
                            sys->mqtt_error_count++;
                        }
                        free(json_string);
                    }
                    cJSON_Delete(json);
                }
            } else {
                ESP_LOGI(TAG, "⏳ No fresh GPS data, skipping publish cycle");
            }
        }
        
        // Handle incoming messages
        task_message_t message;
        if (receive_task_message(sys->mqtt_queue, &message, 100)) {
            // Handle MQTT-specific commands
            if (message.type == MSG_TYPE_COMMAND && message.data) {
                mqtt_cmd_t* cmd = (mqtt_cmd_t*)message.data;
                
                switch (*cmd) {
                    case MQTT_CMD_CONNECT:
                        ESP_LOGI(TAG, "📨 Received CONNECT command");
                        if (mqtt_initialized && mqtt_if && mqtt_if->connect) {
                            mqtt_if->connect();
                        }
                        break;
                        
                    case MQTT_CMD_DISCONNECT:
                        ESP_LOGI(TAG, "📨 Received DISCONNECT command");
                        if (mqtt_if && mqtt_if->disconnect) {
                            mqtt_if->disconnect();
                            mqtt_connected = false;
                            xEventGroupClearBits(sys->system_events, EVENT_MQTT_READY);
                        }
                        break;
                        
                    case MQTT_CMD_PUBLISH:
                        ESP_LOGI(TAG, "📨 Received PUBLISH command");
                        // Trigger immediate publish on next cycle
                        last_publish = 0;
                        break;
                        
                    case MQTT_CMD_RESET_CLIENT:
                        ESP_LOGI(TAG, "📨 Received RESET_CLIENT command");
                        mqtt_initialized = false;
                        mqtt_connected = false;
                        xEventGroupClearBits(sys->system_events, EVENT_MQTT_READY);
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Unknown MQTT command: %d", *cmd);
                        break;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
    
    // Cleanup
    if (mqtt_connected && mqtt_if && mqtt_if->disconnect) {
        mqtt_if->disconnect();
    }
    
    if (mqtt_initialized && mqtt_if && mqtt_if->deinit) {
        mqtt_if->deinit();
    }
    
    esp_task_wdt_delete(NULL);
    sys->mqtt_task.state = TASK_STATE_SHUTDOWN;
    ESP_LOGI(TAG, "📨 MQTT task shutdown complete");
    vTaskDelete(NULL);
}