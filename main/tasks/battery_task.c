#include "task_system.h"
#include "modules/battery/battery_module.h"
#include "esp_task_wdt.h"

// External reference to system config
extern tracker_system_config_t system_config;

static const char *TAG = "BATTERY_TASK";

void battery_task_entry(void* parameters) {
    task_system_t* sys = (task_system_t*)parameters;
    
    ESP_LOGI(TAG, "🔋 Battery Task started on Core %d", xPortGetCoreID());
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    
    sys->battery_task.state = TASK_STATE_RUNNING;
    sys->battery_task.current_cpu = xPortGetCoreID();
    
    // Get battery module interface
    const battery_interface_t* battery_if = battery_get_interface();
    
    bool battery_initialized = false;
    uint32_t init_retry_count = 0;
    const uint32_t MAX_INIT_RETRIES = 3;
    const uint32_t BATTERY_READ_INTERVAL = 60000; // 1 minute
    uint32_t last_battery_read = 0;
    
    while (sys->system_running) {
        esp_task_wdt_reset();
        update_task_heartbeat("battery");
        
        uint32_t current_time = get_current_timestamp_ms();
        
        // Handle initialization phase
        if (!battery_initialized) {
            ESP_LOGI(TAG, "🔧 Initializing battery module (attempt %lu/%d)", 
                     init_retry_count + 1, MAX_INIT_RETRIES);
            
            if (battery_if && battery_if->init) {
                if (battery_if->init(&system_config.battery)) {
                    ESP_LOGI(TAG, "✅ Battery module initialized successfully");
                    battery_initialized = true;
                    init_retry_count = 0;
                    sys->battery_task.state = TASK_STATE_READY;
                } else {
                    init_retry_count++;
                    ESP_LOGW(TAG, "⚠️  Battery initialization failed, retry %lu/%d", 
                             init_retry_count, MAX_INIT_RETRIES);
                    
                    if (init_retry_count >= MAX_INIT_RETRIES) {
                        ESP_LOGE(TAG, "❌ Battery initialization failed after %d retries", MAX_INIT_RETRIES);
                        sys->battery_task.state = TASK_STATE_ERROR;
                        sys->battery_error_count++;
                        init_retry_count = 0; // Reset for next attempt cycle
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
                }
            }
        }
        
        // Handle periodic battery monitoring
        else if (battery_initialized && (current_time - last_battery_read) >= BATTERY_READ_INTERVAL) {
            last_battery_read = current_time;
            
            if (battery_if && battery_if->read_data) {
                battery_data_t battery_data = {0};
                if (battery_if->read_data(&battery_data)) {
                    xEventGroupSetBits(sys->system_events, EVENT_BATTERY_DATA_READY);
                    
                    ESP_LOGI(TAG, "🔋 Battery: %.2fV (%d%%) %s", 
                             battery_data.voltage, 
                             (int)battery_data.percentage,
                             battery_data.charging ? "Charging" : "Discharging");
                    
                    // Check for low battery conditions
                    if (battery_data.percentage <= 5) {
                        ESP_LOGW(TAG, "⚠️  CRITICAL: Battery at %d%%, system may shutdown soon", 
                                 (int)battery_data.percentage);
                    } else if (battery_data.percentage <= 15) {
                        ESP_LOGW(TAG, "⚠️  LOW: Battery at %d%%, consider charging", 
                                 (int)battery_data.percentage);
                    }
                    
                    // TODO: Store battery data in shared memory for MQTT task
                    
                } else {
                    ESP_LOGW(TAG, "⚠️  Failed to read battery data");
                    sys->battery_error_count++;
                }
            }
        }
        
        // Handle incoming messages (battery task has minimal commands)
        task_message_t message;
        if (receive_task_message(sys->battery_queue, &message, 100)) {
            // Handle battery-specific commands
            if (message.type == MSG_TYPE_COMMAND) {
                ESP_LOGI(TAG, "📨 Received command message");
                // Battery task has minimal commands, mostly just read requests
                if (battery_initialized && battery_if && battery_if->read_data) {
                    battery_data_t battery_data = {0};
                    if (battery_if->read_data(&battery_data)) {
                        ESP_LOGI(TAG, "📊 Manual battery reading: %.2fV (%d%%)", 
                                 battery_data.voltage, (int)battery_data.percentage);
                    }
                }
            }
        }
        
        // Battery task runs at lower frequency to save power
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
    
    // Cleanup
    if (battery_initialized && battery_if && battery_if->deinit) {
        battery_if->deinit();
    }
    
    esp_task_wdt_delete(NULL);
    sys->battery_task.state = TASK_STATE_SHUTDOWN;
    ESP_LOGI(TAG, "🔋 Battery task shutdown complete");
    vTaskDelete(NULL);
}