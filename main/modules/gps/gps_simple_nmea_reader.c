/**
 * @file gps_simple_nmea_reader.c
 * @brief Simple NMEA reader - just dump raw UART data to see if GPS outputs anything
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

static const char *TAG = "GPS_NMEA_READER";

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
    printf("\n=== SIMPLE NMEA READER INIT ===\n");
    
    if (!config) {
        printf("ERROR: Configuration is NULL\n");
        return false;
    }
    
    printf("Attempting basic GPS initialization...\n");
    if (!gps_power_on_impl()) {
        printf("ERROR: Failed to power on GPS\n");
        return false;
    }
    
    gps_status.initialized = true;
    printf("GPS NMEA reader initialized\n");
    printf("\n");
    return true;
}

static bool gps_deinit_impl(void)
{
    printf("GPS NMEA reader deinit\n");
    gps_status.initialized = false;
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!data) {
        printf("ERROR: GPS data pointer is NULL\n");
        return false;
    }

    printf("\n=== RAW NMEA READER ===\n");
    
    // Clear data structure
    memset(data, 0, sizeof(gps_data_t));
    data->fix_valid = false;

    // Get LTE interface for raw data reading
    const lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        printf("ERROR: LTE interface is NULL\n");
        return false;
    }

    if (!lte->read_raw_data) {
        printf("ERROR: LTE read_raw_data function is NULL\n");
        return false;
    }

    // Read raw UART data for 10 seconds to see ANY output
    char buffer[2048];
    size_t bytes_read = 0;
    
    printf("Reading raw UART for 10 seconds to detect ANY GPS output...\n");
    
    bool read_success = lte->read_raw_data(buffer, sizeof(buffer) - 1, &bytes_read, 10000);
    
    printf("Raw read result: %s\n", read_success ? "SUCCESS" : "FAILED");
    printf("Bytes read: %zu\n", bytes_read);
    
    if (read_success && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        printf("\n=== RAW UART OUTPUT ===\n");
        printf("Length: %zu bytes\n", bytes_read);
        printf("Raw data:\n%s\n", buffer);
        printf("======================\n");
        
        // Look for NMEA sentences
        bool found_nmea = false;
        char* pos = buffer;
        while ((pos = strchr(pos, '$')) != NULL) {
            char* end = strchr(pos, '\n');
            if (end) {
                int len = end - pos;
                if (len > 5 && len < 100) {  // Reasonable NMEA length
                    printf("NMEA SENTENCE FOUND: %.*s\n", len, pos);
                    found_nmea = true;
                }
            }
            pos++;
        }
        
        if (found_nmea) {
            printf("✅ NMEA SENTENCES DETECTED!\n");
        } else {
            printf("❌ NO NMEA SENTENCES FOUND\n");
        }
        
    } else {
        printf("❌ NO RAW UART DATA RECEIVED\n");
    }

    printf("=========================\n\n");

    return false; // Always return false since we're just reading raw data
}

static bool gps_get_status_impl(gps_status_t* status)
{
    if (!status) return false;
    *status = gps_status;
    return true;
}

static bool gps_power_on_impl(void)
{
    printf("\n=== TESTING SIM7670G GPS INITIALIZATION METHODS ===\n");
    
    const lte_interface_t* lte = lte_get_interface();
    if (!lte) {
        printf("ERROR: LTE interface is NULL\n");
        return false;
    }

    if (!lte->send_at_command) {
        printf("ERROR: LTE send_at_command function is NULL\n");
        return false;
    }

    at_response_t response = {0};
    
    // Method 1: CORRECT SIM7670G sequence from Arduino code
    printf("\n--- METHOD 1: CORRECT SIM7670G GPS SEQUENCE ---\n");
    printf("Following Arduino working code sequence:\n");
    
    printf("Step 1: AT+CGNSSPWR=1 (GPS power)\n");
    bool power_success = lte->send_at_command("AT+CGNSSPWR=1", &response, 5000);
    printf("Result: %s | Response: %s\n", power_success ? "SUCCESS" : "FAILED", response.response);
    
    if (power_success && response.success) {
        printf("Waiting 1 second (Arduino delay)...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        memset(&response, 0, sizeof(response));
        printf("Step 2: AT+CGNSSTST=1 (GNSS test mode)\n");
        bool nmea_success = lte->send_at_command("AT+CGNSSTST=1", &response, 5000);
        printf("Result: %s | Response: %s\n", nmea_success ? "SUCCESS" : "FAILED", response.response);
        
        if (nmea_success && response.success) {
            printf("Waiting 1 second (Arduino delay)...\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            memset(&response, 0, sizeof(response));
            printf("Step 3: AT+CGNSSPORTSWITCH=0,1 (CRITICAL - Switch to UART output!)\n");
            bool port_success = lte->send_at_command("AT+CGNSSPORTSWITCH=0,1", &response, 5000);
            printf("Result: %s | Response: %s\n", port_success ? "SUCCESS" : "FAILED", response.response);
            
            if (port_success && response.success) {
                printf("✅ COMPLETE ARDUINO SEQUENCE SUCCESSFUL!\n");
                printf("✅ GPS should now output NMEA data to UART!\n");
                gps_status.gps_power_on = true;
                gps_status.data_output_enabled = true;
            } else {
                printf("❌ Port switch failed - this is the critical missing step!\n");
            }
        } else {
            printf("❌ GNSS test mode failed\n");
        }
    } else {
        printf("❌ GPS power failed\n");
    }
    
    // Method 2: Try alternative GPS commands
    printf("\n--- METHOD 2: Alternative GPS commands ---\n");
    memset(&response, 0, sizeof(response));
    printf("Testing AT+CGPS=1 (alternative GPS command)\n");
    bool alt_gps = lte->send_at_command("AT+CGPS=1", &response, 5000);
    printf("AT+CGPS=1 Result: %s | Response: %s\n", alt_gps ? "SUCCESS" : "FAILED", response.response);
    
    // Method 3: Check GPS status commands
    printf("\n--- METHOD 3: GPS status verification ---\n");
    memset(&response, 0, sizeof(response));
    printf("Testing AT+CGNSSPWR? (check power status)\n");
    bool check_power = lte->send_at_command("AT+CGNSSPWR?", &response, 3000);
    printf("Power Status: %s | Response: %s\n", check_power ? "SUCCESS" : "FAILED", response.response);
    
    memset(&response, 0, sizeof(response));
    printf("Testing AT+CGNSSTST? (check NMEA status)\n");
    bool check_nmea = lte->send_at_command("AT+CGNSSTST?", &response, 3000);
    printf("NMEA Status: %s | Response: %s\n", check_nmea ? "SUCCESS" : "FAILED", response.response);
    
    // Method 4: Test problematic commands
    printf("\n--- METHOD 4: Testing problematic commands ---\n");
    memset(&response, 0, sizeof(response));
    printf("Testing AT+CGNSS=1 (GNSS engine - was failing)\n");
    bool gnss_engine = lte->send_at_command("AT+CGNSS=1", &response, 5000);
    printf("GNSS Engine: %s | Response: %s\n", gnss_engine ? "SUCCESS" : "FAILED", response.response);
    
    memset(&response, 0, sizeof(response));
    printf("Testing AT+CGNSINF (GPS info - was failing)\n");
    bool gps_info = lte->send_at_command("AT+CGNSINF", &response, 5000);
    printf("GPS Info: %s | Response: %s\n", gps_info ? "SUCCESS" : "FAILED", response.response);
    
    printf("\n--- WAITING FOR NMEA DATA (10 seconds) ---\n");
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    printf("=== SIM7670G GPS INITIALIZATION TEST COMPLETE ===\n\n");
    return true;
}

static bool gps_power_off_impl(void)
{
    printf("GPS POWER OFF\n");
    gps_status.gps_power_on = false;
    gps_status.data_output_enabled = false;
    return true;
}

static bool gps_reset_impl(void)
{
    printf("GPS RESET\n");
    return true;
}

static void gps_set_debug_impl(bool enable)
{
    printf("GPS debug %s\n", enable ? "enabled" : "disabled");
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
    return 0.0f;
}

bool gps_format_coordinates(const gps_data_t* data, char* buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size == 0) return false;
    
    snprintf(buffer, buffer_size, "Lat: %.6f, Lon: %.6f", 
             data->latitude, data->longitude);
    return true;
}