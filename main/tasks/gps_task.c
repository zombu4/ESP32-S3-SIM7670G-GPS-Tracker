#include "task_system.h"
#include "modules/gps/gps_module.h"
#include "esp_task_wdt.h"

// External reference to system config
extern tracker_system_config_t system_config;

static const char *TAG = "GPS_TASK";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
void gps_task_entry(void* parameters) {
    task_system_t* sys = (task_system_t*)parameters;
    
    ESP_LOGI(TAG, "🛰️  GPS Task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "🛰️  GPS sys pointer: %p", sys);
    ESP_LOGI(TAG, "🛰️  GPS system_running: %s", sys ? (sys->system_running ? "TRUE" : "FALSE") : "NULL_SYS");
    
    if (!sys) {
        ESP_LOGE(TAG, "❌ GPS task received NULL system pointer!");
        goto cleanup;
    }
    
    // Initialize variables before any potential goto
    const gps_interface_t* gps_if = NULL;
    bool gps_initialized = false;
    
    if (!sys->system_running) {
        ESP_LOGW(TAG, "⚠️  GPS task: system_running is FALSE, waiting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!sys->system_running) {
            ESP_LOGW(TAG, "⚠️  GPS task: system still not running after wait, exiting");
            goto cleanup;
        }
    }
    
    // Initialize GPS interface
    gps_if = gps_get_interface();
    if (!gps_if) {
        ESP_LOGE(TAG, "[GPS_TASK] FATAL: Failed to get GPS interface");
        goto cleanup;
    }
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    
    sys->gps_task.state = TASK_STATE_RUNNING;
    sys->gps_task.current_cpu = xPortGetCoreID();
    bool gps_has_fix = false;
    uint32_t init_retry_count = 0;
    const uint32_t MAX_INIT_RETRIES = 3;
    const uint32_t GPS_POLL_INTERVAL = 10000; // 10 seconds
    const uint32_t GPS_FIX_CHECK_INTERVAL = 5000; // 5 seconds
    uint32_t last_gps_poll = 0;
    uint32_t last_fix_check = 0;
    
    while (sys->system_running) {
        esp_task_wdt_reset();
        update_task_heartbeat("gps");
        
        uint32_t current_time = get_current_timestamp_ms();
        
        // Handle initialization phase
        if (!gps_initialized) {
            ESP_LOGI(TAG, "🔧 Initializing GPS module (attempt %lu/%d)", 
                     init_retry_count + 1, MAX_INIT_RETRIES);
            
            if (gps_if && gps_if->init) {
                if (gps_if->init(&system_config.gps)) {
                    ESP_LOGI(TAG, "✅ GPS module initialized successfully");
                    gps_initialized = true;
                    init_retry_count = 0;
                    sys->gps_task.state = TASK_STATE_READY;
                } else {
                    init_retry_count++;
                    ESP_LOGW(TAG, "⚠️  GPS initialization failed, retry %lu/%d", 
                             init_retry_count, MAX_INIT_RETRIES);
                    
                    if (init_retry_count >= MAX_INIT_RETRIES) {
                        ESP_LOGE(TAG, "❌ GPS initialization failed after %d retries", MAX_INIT_RETRIES);
                        sys->gps_task.state = TASK_STATE_ERROR;
                        sys->gps_error_count++;
                        init_retry_count = 0; // Reset for next attempt cycle
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait before retry
                }
            }
        }
        
        // Handle GPS fix monitoring
        else if (gps_initialized && (current_time - last_fix_check) >= GPS_FIX_CHECK_INTERVAL) {
            last_fix_check = current_time;
            
            if (gps_if && gps_if->read_data) {
                gps_data_t current_gps_data = {0};
                bool data_read = gps_if->read_data(&current_gps_data);
                bool currently_has_fix = data_read && gps_is_fix_valid(&current_gps_data);
                
                if (currently_has_fix && !gps_has_fix) {
                    // GPS fix acquired
                    gps_has_fix = true;
                    xEventGroupSetBits(sys->system_events, EVENT_GPS_FIX_ACQUIRED);
                    xEventGroupClearBits(sys->system_events, EVENT_GPS_FIX_LOST);
                    ESP_LOGI(TAG, "🎯 GPS fix acquired!");
                    
                } else if (!currently_has_fix && gps_has_fix) {
                    // GPS fix lost
                    gps_has_fix = false;
                    xEventGroupClearBits(sys->system_events, EVENT_GPS_FIX_ACQUIRED);
                    xEventGroupSetBits(sys->system_events, EVENT_GPS_FIX_LOST);
                    ESP_LOGW(TAG, "⚠️  GPS fix lost");
                    sys->gps_error_count++;
                }
            }
        }
        
        // Handle periodic GPS polling when we have a fix
        if (gps_initialized && gps_has_fix && (current_time - last_gps_poll) >= GPS_POLL_INTERVAL) {
            last_gps_poll = current_time;
            
            if (gps_if && gps_if->read_data) {
                gps_data_t gps_data = {0};
                if (gps_if->read_data(&gps_data)) {
                    // Set fresh data event
                    xEventGroupSetBits(sys->system_events, EVENT_GPS_DATA_FRESH);
                    
                    ESP_LOGI(TAG, "📍 GPS Data: Lat=%.6f, Lon=%.6f, Alt=%.1f, Sat=%d", 
                             gps_data.latitude, gps_data.longitude, 
                             gps_data.altitude, gps_data.satellites);
                    
                    // TODO: Store GPS data in shared memory or send to MQTT task
                } else {
                    ESP_LOGW(TAG, "⚠️  Failed to get GPS location data");
                }
            }
        }
        
        // Handle incoming messages
        task_message_t message;
        if (receive_task_message(sys->gps_queue, &message, 100)) {
            // Handle GPS-specific commands
            if (message.type == MSG_TYPE_COMMAND && message.data) {
                gps_cmd_t* cmd = (gps_cmd_t*)message.data;
                
                switch (*cmd) {
                    case GPS_CMD_START:
                        ESP_LOGI(TAG, "📨 Received START command");
                        if (!gps_initialized) {
                            gps_initialized = false; // Trigger re-init
                        }
                        break;
                        
                    case GPS_CMD_STOP:
                        ESP_LOGI(TAG, "📨 Received STOP command");
                        if (gps_if && gps_if->deinit) {
                            gps_if->deinit();
                            gps_initialized = false;
                            gps_has_fix = false;
                            xEventGroupClearBits(sys->system_events, EVENT_GPS_FIX_ACQUIRED);
                        }
                        break;
                        
                    case GPS_CMD_POLL_LOCATION:
                        ESP_LOGI(TAG, "📨 Received POLL_LOCATION command");
                        if (gps_initialized && gps_if && gps_if->read_data) {
                            gps_data_t gps_data = {0};
                            if (gps_if->read_data(&gps_data)) {
                                ESP_LOGI(TAG, "📍 Manual Poll: Lat=%.6f, Lon=%.6f", 
                                         gps_data.latitude, gps_data.longitude);
                            }
                        }
                        break;
                        
                    case GPS_CMD_RESET_MODULE:
                        ESP_LOGI(TAG, "📨 Received RESET_MODULE command");
                        gps_initialized = false;
                        gps_has_fix = false;
                        xEventGroupClearBits(sys->system_events, EVENT_GPS_FIX_ACQUIRED);
                        xEventGroupClearBits(sys->system_events, EVENT_GPS_DATA_FRESH);
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Unknown GPS command: %d", *cmd);
                        break;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
    
cleanup:
    // Cleanup - check if variables were properly initialized
    if (gps_if != NULL && gps_initialized && gps_if->deinit) {
        gps_if->deinit();
    }
    
    esp_task_wdt_delete(NULL);
    if (sys) {
        sys->gps_task.state = TASK_STATE_SHUTDOWN;
    }
    ESP_LOGI(TAG, "🛰️  GPS task shutdown complete");
    vTaskDelete(NULL);
}
#pragma GCC diagnostic pop