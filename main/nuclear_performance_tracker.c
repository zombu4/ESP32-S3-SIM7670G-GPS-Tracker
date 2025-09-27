/**
 * 💀🔥 NUCLEAR PERFORMANCE TRACKER 🔥💀
 * 
 * Tracks nuclear acceleration performance gains and metrics
 */

#include "nuclear_acceleration.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "NUCLEAR_PERF_TRACKER";

// Performance tracking task
static void nuclear_performance_tracker_task(void *pvParameters)
{
    const nuclear_acceleration_interface_t* nuke_if = nuclear_acceleration_get_interface();
    if (!nuke_if) {
        ESP_LOGE(TAG, "❌ Nuclear acceleration interface not available");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "🚀 Nuclear Performance Tracker started on Core %d", xPortGetCoreID());
    
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        char metrics[512];
        
        // Get current nuclear performance metrics
        if (nuke_if->get_performance_metrics) {
            nuke_if->get_performance_metrics(metrics, sizeof(metrics));
            ESP_LOGI(TAG, "📊 NUCLEAR METRICS: %s", metrics);
        }
        
        // Check if acceleration is still active
        if (nuke_if->is_acceleration_active && nuke_if->is_acceleration_active()) {
            ESP_LOGD(TAG, "💀🔥 NUCLEAR ACCELERATION FULLY OPERATIONAL! 🔥💀");
        } else {
            ESP_LOGW(TAG, "⚠️ Nuclear acceleration not active");
        }
        
        // Wait 30 seconds before next metrics report
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(30000));
    }
}

// Initialize nuclear performance tracking
esp_err_t nuclear_start_performance_tracking(void)
{
    BaseType_t result = xTaskCreatePinnedToCore(
        nuclear_performance_tracker_task,
        "nuclear_perf_tracker",
        4096,
        NULL,
        2, // Lower priority than main tasks
        NULL,
        1  // Pin to core 1
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create nuclear performance tracker task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "✅ Nuclear performance tracking started");
    return ESP_OK;
}