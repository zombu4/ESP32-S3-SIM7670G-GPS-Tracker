/**
 * 💀🔥💀 NUCLEAR PIPELINE STARTUP IMPLEMENTATION 💀🔥💀
 */

#include "nuclear_startup.h"
#include "nuclear_integration.h"
#include "esp_log.h"

static const char *TAG = "NUCLEAR_STARTUP";

// Global startup state
static nuclear_integration_manager_t g_nuclear_manager;
static bool nuclear_system_ready = false;

// Default configuration
const nuclear_startup_config_t NUCLEAR_STARTUP_DEFAULT_CONFIG = {
    .enable_nuclear_pipeline = true,
    .enable_debug_logging = true,
    .enable_performance_monitoring = true
};

esp_err_t nuclear_startup_init(const nuclear_startup_config_t *config)
{
    if (!config) {
        config = &NUCLEAR_STARTUP_DEFAULT_CONFIG;
    }
    
    ESP_LOGI(TAG, "💀🔥 NUCLEAR PIPELINE STARTUP INITIALIZATION 🔥💀");
    
    if (!config->enable_nuclear_pipeline) {
        ESP_LOGI(TAG, "Nuclear pipeline disabled in configuration");
        return ESP_OK;
    }
    
    // Initialize nuclear integration system
    esp_err_t ret = nuclear_integration_init(&g_nuclear_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize nuclear integration");
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Nuclear pipeline system initialized");
    return ESP_OK;
}

esp_err_t nuclear_startup_begin(void)
{
    ESP_LOGI(TAG, "🚀 STARTING NUCLEAR PIPELINE SYSTEM...");
    
    // Start nuclear integration
    esp_err_t ret = nuclear_integration_start(&g_nuclear_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start nuclear integration");
        return ret;
    }
    
    nuclear_system_ready = true;
    
    ESP_LOGI(TAG, "💀🔥 NUCLEAR PIPELINE SYSTEM ACTIVE - PARALLEL PROCESSING ENGAGED! 🔥💀");
    return ESP_OK;
}

bool nuclear_startup_is_ready(void)
{
    return nuclear_system_ready && nuclear_integration_is_active();
}

void nuclear_startup_get_performance_stats(uint32_t *total_bytes, 
                                         uint32_t *cellular_packets,
                                         uint32_t *gps_packets,
                                         uint32_t *errors)
{
    if (!nuclear_system_ready) {
        if (total_bytes) *total_bytes = 0;
        if (cellular_packets) *cellular_packets = 0;
        if (gps_packets) *gps_packets = 0;
        if (errors) *errors = 0;
        return;
    }
    
    // Get integration stats
    uint32_t gps_reads, cellular_reads, integration_errors;
    nuclear_integration_get_stats(&g_nuclear_manager, &gps_reads, &cellular_reads, &integration_errors);
    
    // Get pipeline stats
    uint32_t pipeline_total, pipeline_cellular, pipeline_gps, pipeline_errors;
    nuclear_pipeline_get_stats(g_nuclear_manager.pipeline, &pipeline_total, &pipeline_cellular, &pipeline_gps, &pipeline_errors);
    
    if (total_bytes) *total_bytes = pipeline_total;
    if (cellular_packets) *cellular_packets = pipeline_cellular;
    if (gps_packets) *gps_packets = pipeline_gps;
    if (errors) *errors = integration_errors + pipeline_errors;
}

esp_err_t nuclear_startup_shutdown(void)
{
    ESP_LOGI(TAG, "🛑 Shutting down nuclear pipeline system...");
    
    nuclear_system_ready = false;
    
    esp_err_t ret = nuclear_integration_deinit(&g_nuclear_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize nuclear integration");
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Nuclear pipeline system shutdown complete");
    return ESP_OK;
}