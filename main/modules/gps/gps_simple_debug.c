/**
 * @file gps_simple_debug.c
 * @brief Ultra-simple GPS debug module - just show raw data
 *
 * This module ONLY shows raw GPS data from SIM7670G without parsing.
 * Purpose: Debug what data is actually being received.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "gps_module.h"
#include "../lte/lte_module.h"

static const char *TAG = "GPS_DEBUG";
static bool gps_debug_enabled = true;

// Simple GPS status
static gps_status_t gps_status = {
    .initialized = false,
    .uart_ready = false,
    .gps_power_on = false,
    .gnss_enabled = false,
    .data_output_enabled = false,
    .last_fix_time = 0,
    .total_sentences_parsed = 0,
    .valid_sentences = 0,
    .parse_errors = 0
};

// Function declarations
static bool gps_init_impl(const gps_config_t* config);
static bool gps_deinit_impl(void);
static bool gps_read_data_impl(gps_data_t* data);
static bool gps_get_status_impl(gps_status_t* status);
static bool gps_power_on_impl(void);
static bool gps_power_off_impl(void);
static bool gps_reset_impl(void);
static void gps_set_debug_impl(bool enable);

// GPS interface
static const gps_interface_t gps_interface = {
    .init = gps_init_impl,
    .deinit = gps_deinit_impl,
    .read_data = gps_read_data_impl,
    .get_status = gps_get_status_impl,
    .power_on = gps_power_on_impl,
    .power_off = gps_power_off_impl,
    .reset = gps_reset_impl,
    .set_debug = gps_set_debug_impl
};

const gps_interface_t* gps_get_interface(void)
{
    return &gps_interface;
}

static bool gps_init_impl(const gps_config_t* config)
{
    ESP_LOGI(TAG, "🚀 GPS Simple Debug Module initializing...");
    
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    // Power on GPS hardware - this is critical!
    ESP_LOGI(TAG, "🔋 Powering on GPS hardware...");
    if (!gps_power_on_impl()) {
        ESP_LOGE(TAG, "❌ Failed to power on GPS hardware");
        return false;
    }
    
    gps_status.initialized = true;
    ESP_LOGI(TAG, "✅ GPS Simple Debug Module initialized successfully");
    return true;
}

static bool gps_deinit_impl(void)
{
    ESP_LOGI(TAG, "GPS Simple Debug Module deinitialized");
    gps_status.initialized = false;
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!data) {
        ESP_LOGE(TAG, "GPS data pointer is NULL");
        return false;
    }

    ESP_LOGI(TAG, "🔍 GPS READ DATA - Checking for raw GPS output...");
    
    // Clear data structure
    memset(data, 0, sizeof(gps_data_t));
    data->fix_valid = false;

    // Get LTE interface for raw data reading
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->read_raw_data) {
        ESP_LOGW(TAG, "⚠️  LTE interface not available for raw data reading");
        return false;
    }

    // Read raw data from UART
    char buffer[1024];
    size_t bytes_read = 0;
    
    ESP_LOGI(TAG, "📡 Reading raw UART data for 5 seconds...");
    
    if (lte->read_raw_data(buffer, sizeof(buffer) - 1, &bytes_read, 5000)) {
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // Null terminate
            
            ESP_LOGI(TAG, "📋 RAW GPS DATA RECEIVED [%zu bytes]:", bytes_read);
            ESP_LOGI(TAG, "📋 %s", buffer);
            
            // Check for NMEA sentences
            if (strstr(buffer, "$G") != NULL) {
                ESP_LOGI(TAG, "✅ NMEA SENTENCES DETECTED!");
                if (strstr(buffer, "$GPRMC") != NULL) {
                    ESP_LOGI(TAG, "✅ Found $GPRMC (Recommended Minimum)");
                }
                if (strstr(buffer, "$GPGGA") != NULL) {
                    ESP_LOGI(TAG, "✅ Found $GPGGA (Global Positioning System Fix Data)");
                }
            } else {
                ESP_LOGI(TAG, "⚠️  No NMEA sentences detected in raw data");
            }
        } else {
            ESP_LOGI(TAG, "⚠️  No raw data received from UART");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  Failed to read raw UART data");
    }

    // Try AT+CGNSINF polling method
    ESP_LOGI(TAG, "🔍 Testing Waveshare AT+CGNSINF polling method...");
    at_response_t response = {0};
    
    if (lte->send_at_command && lte->send_at_command("AT+CGNSINF", &response, 3000)) {
        ESP_LOGI(TAG, "📋 AT+CGNSINF RESPONSE: %s", response.response);
        
        // Check if we have GPS data
        if (strlen(response.response) > 10) {
            ESP_LOGI(TAG, "✅ AT+CGNSINF returned GPS data!");
        } else {
            ESP_LOGI(TAG, "⚠️  AT+CGNSINF returned minimal data");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  AT+CGNSINF command failed");
    }

    ESP_LOGI(TAG, "🔍 GPS DEBUG COMPLETE");
    ESP_LOGI(TAG, "💡 If no data shown above, GPS may need:");
    ESP_LOGI(TAG, "💡   - Outdoor location with clear sky view");
    ESP_LOGI(TAG, "💡   - GPS antenna connected properly");
    ESP_LOGI(TAG, "💡   - GPS power enabled (AT+CGNSSPWR=1)");
    ESP_LOGI(TAG, "💡   - NMEA output enabled (AT+CGNSSTST=1)");

    return false; // Always return false since we're just debugging
}

static bool gps_get_status_impl(gps_status_t* status)
{
    if (!status) return false;
    *status = gps_status;
    return true;
}

static bool gps_power_on_impl(void)
{
    ESP_LOGI(TAG, "🔋 GPS power on requested - using LTE interface");
    
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE interface not available for GPS commands");
        return false;
    }

    at_response_t response = {0};
    
    // Enable GPS power
    ESP_LOGI(TAG, "📡 Sending AT+CGNSSPWR=1...");
    if (lte->send_at_command("AT+CGNSSPWR=1", &response, 5000)) {
        ESP_LOGI(TAG, "✅ GPS power response: %s", response.response);
        gps_status.gps_power_on = true;
    } else {
        ESP_LOGW(TAG, "⚠️  GPS power command failed");
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Enable NMEA output
    ESP_LOGI(TAG, "📡 Sending AT+CGNSSTST=1...");
    if (lte->send_at_command("AT+CGNSSTST=1", &response, 5000)) {
        ESP_LOGI(TAG, "✅ NMEA enable response: %s", response.response);
        gps_status.data_output_enabled = true;
    } else {
        ESP_LOGW(TAG, "⚠️  NMEA enable command failed");
    }

    return true;
}

static bool gps_power_off_impl(void)
{
    ESP_LOGI(TAG, "GPS power off");
    gps_status.gps_power_on = false;
    gps_status.data_output_enabled = false;
    return true;
}

static bool gps_reset_impl(void)
{
    ESP_LOGI(TAG, "GPS reset");
    return true;
}

static void gps_set_debug_impl(bool enable)
{
    gps_debug_enabled = enable;
    ESP_LOGI(TAG, "GPS debug %s", enable ? "enabled" : "disabled");
}

// =============================================================================
// GPS Utility Functions (required by other modules)
// =============================================================================

bool gps_is_fix_valid(const gps_data_t* data)
{
    if (!data) return false;
    return data->fix_valid;
}

float gps_calculate_distance(float lat1, float lon1, float lat2, float lon2)
{
    // Simple distance calculation (not implemented in debug module)
    return 0.0f;
}

bool gps_format_coordinates(const gps_data_t* data, char* buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size == 0) return false;
    
    snprintf(buffer, buffer_size, "Lat: %.6f, Lon: %.6f", 
             data->latitude, data->longitude);
    return true;
}