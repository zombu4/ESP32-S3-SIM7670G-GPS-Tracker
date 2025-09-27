/**
 * @file gps_raw_debug.c
 * @brief RAW GPS DEBUG - Show actual bytes received
 *
 * NO PARSING, NO EMOJIS, JUST RAW DATA
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

static const char *TAG = "GPS_RAW";

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

static void dump_raw_bytes(const char* label, const uint8_t* data, size_t len)
{
    printf("\n=== %s ===\n", label);
    printf("LENGTH: %zu bytes\n", len);
    
    if (len == 0) {
        printf("NO DATA\n");
        return;
    }
    
    // HEX DUMP
    printf("HEX: ");
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n     ");
    }
    printf("\n");
    
    // ASCII DUMP
    printf("ASCII: ");
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("\n");
    
    // RAW STRING (if printable)
    printf("STRING: \"");
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) {
            printf("%c", data[i]);
        } else if (data[i] == '\r') {
            printf("\\r");
        } else if (data[i] == '\n') {
            printf("\\n");
        } else {
            printf("\\x%02X", data[i]);
        }
    }
    printf("\"\n");
    printf("===================\n\n");
}

static bool gps_init_impl(const gps_config_t* config)
{
    printf("\n");
    printf("GPS RAW DEBUG INIT START\n");
    
    if (!config) {
        printf("ERROR: Configuration is NULL\n");
        return false;
    }
    
    printf("Powering on GPS hardware\n");
    if (!gps_power_on_impl()) {
        printf("ERROR: Failed to power on GPS hardware\n");
        return false;
    }
    
    gps_status.initialized = true;
    printf("GPS RAW DEBUG INIT COMPLETE\n");
    printf("\n");
    return true;
}

static bool gps_deinit_impl(void)
{
    printf("GPS RAW DEBUG DEINIT\n");
    gps_status.initialized = false;
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!data) {
        printf("ERROR: GPS data pointer is NULL\n");
        return false;
    }

    printf("\n");
    printf("=== GPS READ DATA START ===\n");
    
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

    // Read raw UART data with maximum verbosity
    char buffer[2048];
    size_t bytes_read = 0;
    
    printf("Reading raw UART data for 3 seconds...\n");
    
    bool read_success = lte->read_raw_data(buffer, sizeof(buffer) - 1, &bytes_read, 3000);
    
    printf("Raw read result: %s\n", read_success ? "SUCCESS" : "FAILED");
    printf("Bytes read: %zu\n", bytes_read);
    
    if (read_success && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        dump_raw_bytes("RAW UART DATA", (uint8_t*)buffer, bytes_read);
    } else {
        printf("NO RAW UART DATA RECEIVED\n");
    }

    // Test AT+CGNSINF with full verbosity
    printf("\n");
    printf("Testing AT+CGNSINF command...\n");
    
    if (!lte->send_at_command) {
        printf("ERROR: LTE send_at_command function is NULL\n");
        return false;
    }
    
    at_response_t response = {0};
    
    printf("Sending: AT+CGNSINF\n");
    bool cmd_success = lte->send_at_command("AT+CGNSINF", &response, 5000);
    
    printf("AT command result: %s\n", cmd_success ? "SUCCESS" : "FAILED");
    printf("Response success flag: %s\n", response.success ? "TRUE" : "FALSE");
    printf("Response time: %lu ms\n", response.response_time_ms);
    
    size_t resp_len = strlen(response.response);
    printf("Response length: %zu\n", resp_len);
    
    if (resp_len > 0) {
        dump_raw_bytes("AT+CGNSINF RESPONSE", (uint8_t*)response.response, resp_len);
    } else {
        printf("AT+CGNSINF RESPONSE IS EMPTY\n");
    }

    // Try to get any preserved NMEA data
    printf("\n");
    printf("Checking for preserved NMEA data...\n");
    
    if (lte->get_preserved_nmea) {
        char nmea_buffer[1024];
        size_t nmea_len = 0;
        
        bool nmea_success = lte->get_preserved_nmea(nmea_buffer, sizeof(nmea_buffer) - 1, &nmea_len);
        
        printf("Preserved NMEA result: %s\n", nmea_success ? "SUCCESS" : "FAILED");
        printf("Preserved NMEA length: %zu\n", nmea_len);
        
        if (nmea_success && nmea_len > 0) {
            nmea_buffer[nmea_len] = '\0';
            dump_raw_bytes("PRESERVED NMEA DATA", (uint8_t*)nmea_buffer, nmea_len);
        } else {
            printf("NO PRESERVED NMEA DATA\n");
        }
    } else {
        printf("get_preserved_nmea function not available\n");
    }

    printf("=== GPS READ DATA END ===\n");
    printf("\n");

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
    printf("\n");
    printf("=== GPS POWER ON START ===\n");
    
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
    
    // Enable GPS power
    printf("Sending AT+CGNSSPWR=1\n");
    bool power_success = lte->send_at_command("AT+CGNSSPWR=1", &response, 5000);
    
    printf("AT+CGNSSPWR=1 result: %s\n", power_success ? "SUCCESS" : "FAILED");
    printf("Response success flag: %s\n", response.success ? "TRUE" : "FALSE");
    printf("Response time: %lu ms\n", response.response_time_ms);
    
    size_t resp_len = strlen(response.response);
    printf("Response length: %zu\n", resp_len);
    
    if (resp_len > 0) {
        dump_raw_bytes("AT+CGNSSPWR=1 RESPONSE", (uint8_t*)response.response, resp_len);
        gps_status.gps_power_on = true;
    } else {
        printf("AT+CGNSSPWR=1 RESPONSE IS EMPTY\n");
        return false;
    }
    
    printf("Waiting 2 seconds for GPS power up...\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Enable GNSS engine (CRITICAL MISSING COMMAND!)
    memset(&response, 0, sizeof(response));
    
    printf("Sending AT+CGNSS=1 (Enable GNSS Engine)\n");
    bool gnss_success = lte->send_at_command("AT+CGNSS=1", &response, 5000);
    
    printf("AT+CGNSS=1 result: %s\n", gnss_success ? "SUCCESS" : "FAILED");
    printf("Response success flag: %s\n", response.success ? "TRUE" : "FALSE");
    printf("Response time: %lu ms\n", response.response_time_ms);
    
    resp_len = strlen(response.response);
    printf("Response length: %zu\n", resp_len);
    
    if (resp_len > 0) {
        dump_raw_bytes("AT+CGNSS=1 RESPONSE", (uint8_t*)response.response, resp_len);
        gps_status.gnss_enabled = true;
    } else {
        printf("AT+CGNSS=1 RESPONSE IS EMPTY\n");
        return false;
    }
    
    printf("Waiting for GNSS engine to start...\n");
    vTaskDelay(pdMS_TO_TICKS(3000)); // Longer wait for GNSS engine
    
    // Enable NMEA output
    memset(&response, 0, sizeof(response));
    
    printf("Sending AT+CGNSSTST=1\n");
    bool nmea_success = lte->send_at_command("AT+CGNSSTST=1", &response, 5000);
    
    printf("AT+CGNSSTST=1 result: %s\n", nmea_success ? "SUCCESS" : "FAILED");
    printf("Response success flag: %s\n", response.success ? "TRUE" : "FALSE");
    printf("Response time: %lu ms\n", response.response_time_ms);
    
    resp_len = strlen(response.response);
    printf("Response length: %zu\n", resp_len);
    
    if (resp_len > 0) {
        dump_raw_bytes("AT+CGNSSTST=1 RESPONSE", (uint8_t*)response.response, resp_len);
        gps_status.data_output_enabled = true;
    } else {
        printf("AT+CGNSSTST=1 RESPONSE IS EMPTY\n");
    }

    printf("=== GPS POWER ON END ===\n");
    printf("\n");

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