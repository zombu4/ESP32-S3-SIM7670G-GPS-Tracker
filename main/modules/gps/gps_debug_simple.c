/**
 * @file gps_debug_simple.c
 * @brief Simple GPS debug function to show raw NMEA data without parsing
 * 
 * This function will replace the complex GPS reading to avoid crashes
 * and just show what data we actually get from the GPS module
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

static const char* TAG = "GPS_DEBUG";

// External interfaces
extern const void* lte_get_interface(void);
extern const void* nuclear_get_interface(void);

/**
 * @brief Simple GPS data reader that just shows raw data without parsing
 * @param data GPS data structure (will set has_fix = false for safety)
 * @return false (we're just debugging, not actually parsing)
 */
bool gps_read_data_simple_debug(void* data)
{
    ESP_LOGI(TAG, "🔍 GPS SIMPLE DEBUG: Reading raw data WITHOUT parsing");
    ESP_LOGI(TAG, "🔍 This will show exactly what the GPS module outputs");
    
    // Set data invalid for safety
    if (data) {
        // Assuming first field is has_fix - set to false
        *((bool*)data) = false;
    }
    
    // Step 1: Try AT+CGNSINF command
    ESP_LOGI(TAG, "🔍 Step 1: Sending AT+CGNSINF command...");
    
    // We'll use direct UART instead of complex AT command functions
    const char* at_cmd = "AT+CGNSINF\r\n";
    uart_write_bytes(UART_NUM_1, at_cmd, strlen(at_cmd));
    
    // Wait for response
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Read response
    char response_buffer[512];
    int response_len = uart_read_bytes(UART_NUM_1, (uint8_t*)response_buffer, 511, pdMS_TO_TICKS(2000));
    
    if (response_len > 0) {
        response_buffer[response_len] = '\0';
        ESP_LOGI(TAG, "📋 RAW AT+CGNSINF RESPONSE [%d bytes]: %s", response_len, response_buffer);
    } else {
        ESP_LOGI(TAG, "⚠️  No response to AT+CGNSINF");
    }
    
    // Step 2: Try to read any NMEA data that might be flowing
    ESP_LOGI(TAG, "🔍 Step 2: Looking for NMEA sentences in UART buffer...");
    
    char nmea_buffer[1024];
    int nmea_len = uart_read_bytes(UART_NUM_1, (uint8_t*)nmea_buffer, 1023, pdMS_TO_TICKS(3000));
    
    if (nmea_len > 0) {
        nmea_buffer[nmea_len] = '\0';
        ESP_LOGI(TAG, "📋 RAW NMEA DATA [%d bytes]: %s", nmea_len, nmea_buffer);
        
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
    
    const void* nuclear_if = nuclear_get_interface();
    if (nuclear_if) {
        ESP_LOGI(TAG, "📡 Nuclear interface available - checking for GPS data");
        // For now, just report that nuclear interface exists
        // We'll implement nuclear pipeline reading later if basic UART works
    } else {
        ESP_LOGI(TAG, "⚠️  Nuclear interface not available");
    }
    
    ESP_LOGI(TAG, "🔍 GPS DEBUG COMPLETE - Check logs above for actual data received");
    ESP_LOGI(TAG, "💡 If no data shown, GPS may need outdoor location or antenna connection");
    
    // Always return false since we're just debugging
    return false;
}