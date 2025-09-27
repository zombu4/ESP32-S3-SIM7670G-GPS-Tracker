/**
 * @file gps_module_clean.c
 * @brief Clean GPS module that just shows raw NMEA data without parsing
 * 
 * This replaces the complex GPsimple debug version
 * that shows exactly what data we get from the GPS module
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "gps_module.h"
#include "../lte/lte_module.h"
#include "../parallel/nuclear_integration.h"

static const char* TAG = "GPS_MODULE";

// Module state
static gps_config_t current_config = {0};
static gps_status_t module_status = {0};
static bool module_initialized = false;

// Function prototypes
static bool gps_init_impl(const gps_config_t* config);
static bool gps_deinit_impl(void);
static bool gps_read_data_impl(gps_data_t* data);
static bool gps_get_status_impl(gps_status_t* status);
static bool gps_power_on_impl(void);
static bool gps_power_off_impl(void);
static bool gps_reset_impl(void);
static void gps_set_debug_impl(bool enable);

// Interface implementation
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
    if (!config) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return false;
    }

    ESP_LOGI(TAG, "🔍 GPS DEBUG MODULE: Initializing simple debug version");
    ESP_LOGI(TAG, "🔍 This module will show raw GPS data without complex parsing");

    memcpy(&current_config, config, sizeof(gps_config_t));
    module_initialized = true;
    
    // Set status
    module_status.gps_power_on = false;
    module_status.gnss_enabled = false;
    module_status.uart_ready = true;
    
    ESP_LOGI(TAG, "✅ GPS debug module initialized");
    return true;
}

static bool gps_deinit_impl(void)
{
    ESP_LOGI(TAG, "GPS module deinitializing");
    module_initialized = false;
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!module_initialized || !data) {
        return false;
    }

    ESP_LOGI(TAG, "🔍 GPS RAW DATA DEBUG: Reading data WITHOUT parsing to avoid crashes");
    ESP_LOGI(TAG, "🔍 This will show exactly what the GPS module outputs");
    
    // Clear data structure for safety
    memset(data, 0, sizeof(gps_data_t));
    data->fix_valid = false;
    
    // Step 1: Try AT+CGNSINF command to check GPS status
    ESP_LOGI(TAG, "🔍 Step 1: Sending AT+CGNSINF command to check GPS status...");
    
    const char* at_cmd = "AT+CGNSINF\r\n";
    uart_write_bytes(UART_NUM_1, at_cmd, strlen(at_cmd));
    
    // Wait for response
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Read response
    char response_buffer[512];
    int response_len = uart_read_bytes(UART_NUM_1, (uint8_t*)response_buffer, 511, pdMS_TO_TICKS(2000));
    
    if (response_len > 0) {
        response_buffer[response_len] = '\0';
        ESP_LOGI(TAG, "📋 RAW AT+CGNSINF RESPONSE [%d bytes]:", response_len);
        ESP_LOGI(TAG, "📋 %s", response_buffer);
    } else {
        ESP_LOGI(TAG, "⚠️  No response to AT+CGNSINF command");
    }
    
    // Step 2: Try to read any NMEA data that might be flowing
    ESP_LOGI(TAG, "🔍 Step 2: Looking for NMEA sentences in UART buffer...");
    
    char nmea_buffer[1024];
    int nmea_len = uart_read_bytes(UART_NUM_1, (uint8_t*)nmea_buffer, 1023, pdMS_TO_TICKS(3000));
    
    if (nmea_len > 0) {
        nmea_buffer[nmea_len] = '\0';
        ESP_LOGI(TAG, "📋 RAW NMEA DATA [%d bytes]:", nmea_len);
        ESP_LOGI(TAG, "📋 %s", nmea_buffer);
        
        // Check if it contains NMEA sentences
        if (strstr(nmea_buffer, "$G") != NULL) {
            ESP_LOGI(TAG, "✅ NMEA SENTENCES DETECTED in raw data!");
        } else {
            ESP_LOGI(TAG, "⚠️  Data received but no NMEA sentences found");
        }
    } else {
        ESP_LOGI(TAG, "⚠️  No NMEA data in UART buffer");
    }
    
    // Step 3: Check nuclear pipeline for any GPS data
    ESP_LOGI(TAG, "🔍 Step 3: Checking nuclear pipeline for GPS data...");
    
    // TODO: Nuclear interface needs proper header include
    ESP_LOGI(TAG, "📡 Nuclear pipeline check - interface not implemented in clean module");
        
        if (pipeline_data && pipeline_size > 0) {
            ESP_LOGI(TAG, "📋 NUCLEAR PIPELINE GPS DATA [%zu bytes]:", pipeline_size);
            ESP_LOGI(TAG, "📋 %.*s", (int)pipeline_size, (char*)pipeline_data);
            
            if (strstr((char*)pipeline_data, "$G") != NULL) {
                ESP_LOGI(TAG, "✅ NMEA SENTENCES DETECTED in nuclear pipeline!");
            } else {
                ESP_LOGI(TAG, "⚠️  Nuclear pipeline data found but no NMEA sentences");
            }
        } else {
            ESP_LOGI(TAG, "⚠️  No GPS data in nuclear pipeline");
        }
    } else {
        ESP_LOGI(TAG, "⚠️  Nuclear interface not available");
    }
    
    ESP_LOGI(TAG, "🔍 GPS DEBUG COMPLETE - Check logs above for actual data received");
    ESP_LOGI(TAG, "💡 If no data shown, GPS may need:");
    ESP_LOGI(TAG, "💡   - Outdoor location with clear sky view");
    ESP_LOGI(TAG, "💡   - GPS antenna connected properly");
    ESP_LOGI(TAG, "💡   - GPS power enabled (AT+CGNSSPWR=1)");
    ESP_LOGI(TAG, "💡   - NMEA output enabled (AT+CGNSSTST=1)");
    
    // Always return false since we're just debugging (not actually parsing GPS data)
    return false;
}

static bool gps_get_status_impl(gps_status_t* status)
{
    if (!status) {
        return false;
    }
    
    memcpy(status, &module_status, sizeof(gps_status_t));
    return true;
}

static bool gps_power_on_impl(void)
{
    ESP_LOGI(TAG, "🔍 GPS power on requested - using LTE interface to send AT commands");
    
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE interface not available for GPS commands");
        return false;
    }
    
    at_response_t response = {0};
    
    // Enable GPS power
    if (lte->send_at_command("AT+CGNSSPWR=1", &response, 5000)) {
        ESP_LOGI(TAG, "✅ GPS power enabled: %s", response.data);
        module_status.gps_power_on = true;
    } else {
        ESP_LOGW(TAG, "⚠️  GPS power command failed");
        return false;
    }
    
    // Enable NMEA output
    if (lte->send_at_command("AT+CGNSSTST=1", &response, 5000)) {
        ESP_LOGI(TAG, "✅ NMEA output enabled: %s", response.data);
        module_status.gnss_enabled = true;
    } else {
        ESP_LOGW(TAG, "⚠️  NMEA output command failed");
    }
    
    return true;
}

static bool gps_power_off_impl(void)
{
    ESP_LOGI(TAG, "GPS power off");
    module_status.gps_power_on = false;
    module_status.gnss_enabled = false;
    return true;
}

static bool gps_reset_impl(void)
{
    ESP_LOGI(TAG, "GPS reset");
    return gps_power_off_impl() && gps_power_on_impl();
}

static void gps_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}