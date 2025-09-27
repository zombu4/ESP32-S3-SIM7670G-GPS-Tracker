#include "task_system.h"
#include "modules/lte/lte_module.h"
#include "modules/modem_init/modem_init.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

// External reference to system config
extern tracker_system_config_t system_config;

static const char *TAG = "CELLULAR_TASK";

void cellular_task_entry(void* parameters) {
    task_system_t* sys = (task_system_t*)parameters;
    
    ESP_LOGI(TAG, "📡 Cellular Task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "🔍 Task system pointer received: %p", sys);
    ESP_LOGI(TAG, "🔍 Initial system_running value: %s", sys ? (sys->system_running ? "TRUE" : "FALSE") : "NULL");
    
    if (!sys) {
        ESP_LOGE(TAG, "❌ CRITICAL: NULL task system pointer received!");
        return;
    }
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    
    sys->cellular_task.state = TASK_STATE_RUNNING;
    sys->cellular_task.current_cpu = xPortGetCoreID();
    
    // Get LTE module interface
    const lte_interface_t* lte_if = lte_get_interface();
    
    bool cellular_initialized = false;
    bool cellular_connected = false;
    uint32_t init_retry_count = 0;
    const uint32_t MAX_INIT_RETRIES = 3;
    const uint32_t HEALTH_CHECK_INTERVAL = 10000; // 10 seconds
    uint32_t last_health_check = 0;
    
    ESP_LOGI(TAG, "🔄 Cellular task entering main loop - system_running: %s", 
             sys->system_running ? "TRUE" : "FALSE");
    ESP_LOGI(TAG, "🔄 Cellular sys pointer: %p", sys);
    ESP_LOGI(TAG, "🔄 About to check system_running flag...");
    
    if (!sys) {
        ESP_LOGE(TAG, "❌ Cellular task received NULL system pointer!");
        goto cleanup;
    }
    
    if (!sys->system_running) {
        ESP_LOGW(TAG, "⚠️  Cellular task: system_running is FALSE, this should NOT happen!");
        ESP_LOGW(TAG, "⚠️  Waiting 5 seconds to see if system becomes ready...");
        // Wait a bit to see if system becomes ready
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!sys->system_running) {
            ESP_LOGW(TAG, "⚠️  Cellular task: system still not running after wait, exiting");
            goto cleanup;
        }
    }
    
    uint32_t loop_count = 0;
    
    while (sys->system_running) {
        loop_count++;
        
        // Prevent watchdog timeout
        esp_task_wdt_reset();
        
        // Verbose logging every 20 iterations
        if (loop_count % 20 == 0) {
            ESP_LOGI(TAG, "📡 Cellular loop #%lu - system_running: %s", 
                     loop_count, sys->system_running ? "TRUE" : "FALSE");
        }
        
        // Get current time for this iteration
        uint32_t current_time = esp_timer_get_time() / 1000; // Convert to ms
        
        // Handle initialization phase
        if (!cellular_initialized) {
            ESP_LOGI(TAG, "🔧 Initializing cellular module (attempt %lu/%d)", 
                     init_retry_count + 1, MAX_INIT_RETRIES);
            
            if (lte_if && lte_if->init) {
                if (lte_if->init(&system_config.lte)) {
                    ESP_LOGI(TAG, "✅ Cellular module initialized successfully");
                    cellular_initialized = true;
                    init_retry_count = 0;
                    
                    // Check if we have data connection
                    lte_status_t status = lte_if->get_connection_status();
                    if (status == LTE_STATUS_CONNECTED) {
                        cellular_connected = true;
                        xEventGroupSetBits(sys->system_events, EVENT_CELLULAR_READY);
                        ESP_LOGI(TAG, "🌐 Cellular data connection established");
                        sys->cellular_task.state = TASK_STATE_READY;
                    }
                } else {
                    init_retry_count++;
                    ESP_LOGW(TAG, "⚠️  Cellular initialization failed, retry %lu/%d", 
                             init_retry_count, MAX_INIT_RETRIES);
                    
                    if (init_retry_count >= MAX_INIT_RETRIES) {
                        ESP_LOGE(TAG, "❌ Cellular initialization failed after %d retries", MAX_INIT_RETRIES);
                        sys->cellular_task.state = TASK_STATE_ERROR;
                        sys->cellular_error_count++;
                        init_retry_count = 0; // Reset for next attempt cycle
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
                }
            }
        }
        
        // Handle connection monitoring
        else if (cellular_initialized && (current_time - last_health_check) >= HEALTH_CHECK_INTERVAL) {
            last_health_check = current_time;
            
            if (lte_if && lte_if->get_connection_status) {
                lte_status_t current_status = lte_if->get_connection_status();
                bool currently_connected = (current_status == LTE_STATUS_CONNECTED);
                
                if (currently_connected && !cellular_connected) {
                    // Connection restored
                    cellular_connected = true;
                    xEventGroupSetBits(sys->system_events, EVENT_CELLULAR_READY);
                    xEventGroupClearBits(sys->system_events, EVENT_CELLULAR_LOST);
                    ESP_LOGI(TAG, "🔄 Cellular connection restored");
                    sys->cellular_task.state = TASK_STATE_READY;
                    
                } else if (!currently_connected && cellular_connected) {
                    // Connection lost
                    cellular_connected = false;
                    xEventGroupClearBits(sys->system_events, EVENT_CELLULAR_READY);
                    xEventGroupSetBits(sys->system_events, EVENT_CELLULAR_LOST);
                    ESP_LOGW(TAG, "⚠️  Cellular connection lost");
                    sys->cellular_task.state = TASK_STATE_ERROR;
                    sys->cellular_error_count++;
                    
                    // Attempt to reconnect
                    if (lte_if->connect) {
                        ESP_LOGI(TAG, "🔄 Attempting to reconnect cellular...");
                        lte_if->connect();
                    }
                }
            }
        }
        
        // Handle incoming messages
        task_message_t message;
        if (receive_task_message(sys->cellular_queue, &message, 100)) {
            // Handle cellular-specific commands
            if (message.type == MSG_TYPE_COMMAND && message.data) {
                cellular_cmd_t* cmd = (cellular_cmd_t*)message.data;
                
                switch (*cmd) {
                    case CELLULAR_CMD_INIT:
                        ESP_LOGI(TAG, "📨 Received INIT command");
                        cellular_initialized = false;
                        break;
                        
                    case CELLULAR_CMD_CONNECT:
                        ESP_LOGI(TAG, "📨 Received CONNECT command");
                        if (lte_if && lte_if->connect) {
                            lte_if->connect();
                        }
                        break;
                        
                    case CELLULAR_CMD_CHECK_SIGNAL:
                        ESP_LOGI(TAG, "📨 Received CHECK_SIGNAL command");
                        if (lte_if && lte_if->get_signal_strength) {
                            int rssi, quality;
                            if (lte_if->get_signal_strength(&rssi, &quality)) {
                                ESP_LOGI(TAG, "📶 Signal strength: RSSI=%d, Quality=%d", rssi, quality);
                            }
                        }
                        break;
                        
                    case CELLULAR_CMD_RESET_MODEM:
                        ESP_LOGI(TAG, "📨 Received RESET_MODEM command");
                        cellular_initialized = false;
                        cellular_connected = false;
                        xEventGroupClearBits(sys->system_events, EVENT_CELLULAR_READY);
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Unknown cellular command: %d", *cmd);
                        break;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
    
cleanup:
    // Cleanup
    esp_task_wdt_delete(NULL);
    if (sys) {
        sys->cellular_task.state = TASK_STATE_SHUTDOWN;
    }
    ESP_LOGI(TAG, "📡 Cellular task shutdown complete");
    vTaskDelete(NULL);
}