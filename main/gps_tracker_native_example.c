/**
 * @brief GPS Tracker with Native Cellular Stack - Usage Example
 * 
 * This demonstrates how to use the new always-on cellular data connection
 * with native TCP/IP networking instead of AT commands.
 * 
 * Benefits:
 * - Always-on data connection (no AT command overhead)
 * - Native TCP/IP stack (standard networking APIs)
 * - Better performance and reliability  
 * - Standard ESP32 networking libraries work seamlessly
 * - Automatic reconnection and health monitoring
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cellular_native_integration.h"
// Include your existing GPS module
// #include "modules/gps/gps_module.h"

static const char *TAG = "GPS_TRACKER_NATIVE";

// Example GPS data (replace with actual GPS module data)
static gps_data_t get_current_gps_data(void)
{
    gps_data_t gps_data = {0};
    
    // Replace this with actual GPS module calls
    gps_data.has_fix = true;  // Get from GPS module
    gps_data.latitude = 26.609140;   // Example coordinates
    gps_data.longitude = -82.114036;
    gps_data.altitude = 10.5;
    gps_data.satellites_used = 8;
    gps_data.timestamp_ms = esp_timer_get_time() / 1000;
    
    return gps_data;
}

void gps_tracker_native_example_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🚀 GPS Tracker with Native Cellular Stack");
    ESP_LOGI(TAG, "🎯 Benefits: Always-on connection, no AT commands, standard networking");
    
    // Step 1: Initialize cellular native integration
    cellular_integration_config_t config = CELLULAR_INTEGRATION_CONFIG_DEFAULT();
    
    // Customize configuration if needed
    strcpy(config.apn, "m2mglobal");
    strcpy(config.mqtt_broker_host, "65.124.194.3");
    config.mqtt_broker_port = 1883;
    strcpy(config.mqtt_topic, "gps_tracker/data");
    config.publish_interval_ms = 30000;  // 30 seconds
    config.debug_enabled = true;
    
    if (!cellular_native_integration_init(&config)) {
        ESP_LOGE(TAG, "❌ Failed to initialize cellular integration");
        vTaskDelete(NULL);
        return;
    }
    
    // Step 2: Start always-on cellular connection
    ESP_LOGI(TAG, "🔄 Starting always-on cellular data connection...");
    if (!cellular_native_integration_start()) {
        ESP_LOGE(TAG, "❌ Failed to start cellular connection");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "✅ Cellular stack ready - always-on data connection established!");
    ESP_LOGI(TAG, "🌐 ESP32 networking APIs now work over cellular");
    
    // Step 3: Test connectivity
    if (cellular_native_integration_test_connectivity()) {
        ESP_LOGI(TAG, "✅ Connectivity test passed");
    } else {
        ESP_LOGW(TAG, "⚠️  Connectivity test failed, but continuing...");
    }
    
    // Step 4: Main GPS tracking loop
    uint32_t last_publish_time = 0;
    
    while (1) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        // Check if it's time to publish
        if ((current_time - last_publish_time) >= config.publish_interval_ms) {
            
            if (cellular_native_integration_is_ready()) {
                
                // Get current GPS data (replace with actual GPS module)
                gps_data_t gps_data = get_current_gps_data();
                
                // Publish GPS data over cellular
                ESP_LOGI(TAG, "📡 Publishing GPS data over cellular...");
                if (cellular_native_integration_publish_gps(&gps_data)) {
                    ESP_LOGI(TAG, "✅ GPS data published successfully");
                    last_publish_time = current_time;
                } else {
                    ESP_LOGE(TAG, "❌ Failed to publish GPS data");
                }
                
            } else {
                ESP_LOGW(TAG, "⚠️  Cellular stack not ready, skipping publish");
            }
        }
        
        // Print status periodically (every 5 minutes)
        static uint32_t last_status_time = 0;
        if ((current_time - last_status_time) >= 300000) {  // 5 minutes
            cellular_native_integration_print_status();
            last_status_time = current_time;
        }
        
        // Sleep for 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Example of using the network interface for custom networking
void example_custom_networking(void)
{
    // Get the network interface
    esp_netif_t* netif = cellular_native_integration_get_netif();
    if (netif) {
        ESP_LOGI(TAG, "🌐 Network interface available for custom networking");
        
        // Now you can use any ESP32 networking library:
        // - HTTP Client: esp_http_client with this network interface
        // - Sockets: Standard Berkeley sockets over cellular
        // - Custom TCP/UDP protocols
        // - Any networking library that accepts esp_netif_t
        
        // Example: You could initialize HTTP client like this:
        /*
        esp_http_client_config_t http_config = {
            .url = "https://api.example.com/data",
            .if_name = netif,  // Use cellular interface
        };
        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        */
    }
}

// Example of custom JSON publishing
void example_custom_json_publish(void)
{
    if (!cellular_native_integration_is_ready()) {
        ESP_LOGW(TAG, "Cellular not ready for custom publish");
        return;
    }
    
    // Create custom JSON data
    const char* custom_json = "{"
        "\"device_id\":\"tracker_001\","
        "\"status\":\"operational\","
        "\"battery_level\":85,"
        "\"signal_strength\":-75,"
        "\"uptime_hours\":24.5"
    "}";
    
    // Publish custom data
    if (cellular_native_integration_publish_json(custom_json)) {
        ESP_LOGI(TAG, "✅ Custom JSON data published");
    } else {
        ESP_LOGE(TAG, "❌ Failed to publish custom data");
    }
}

/* 
 * Integration with existing GPS tracker:
 * 
 * 1. Replace AT command based cellular module with native stack
 * 2. Keep existing GPS module unchanged
 * 3. Replace MQTT AT commands with native MQTT over PPP
 * 4. Enjoy better performance and reliability!
 * 
 * Key changes needed:
 * - Remove old LTE module with AT commands
 * - Use cellular_native_integration for all cellular operations  
 * - Network interface is available for any custom networking
 * - MQTT publishing is simplified with native TCP/IP
 */