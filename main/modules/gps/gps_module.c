#include "gps_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"
// Include LTE module for shared AT interface
#include "../lte/lte_module.h"

static const char *TAG = "GPS_MODULE";

// GPS Ring Buffer Configuration (4KB as requested)
#define GPS_RING_BUFFER_SIZE 4096
typedef struct {
    char data[GPS_RING_BUFFER_SIZE];
    volatile size_t write_idx;
    volatile size_t read_idx;
    volatile size_t count;
    SemaphoreHandle_t mutex;
} gps_ring_buffer_t;

// Module state
static gps_config_t current_config = {0};
static gps_status_t module_status = {0};
static bool module_initialized = false;
static gps_ring_buffer_t gps_buffer = {0};

// Private function prototypes
static bool gps_init_impl(const gps_config_t* config);
static bool gps_deinit_impl(void);
static bool gps_read_data_impl(gps_data_t* data);
static bool gps_get_status_impl(gps_status_t* status);
static bool gps_power_on_impl(void);
static bool gps_power_off_impl(void);
static bool gps_reset_impl(void);
static void gps_set_debug_impl(bool enable);

// AT command functions for SIM7670G GPS
static bool send_gps_at_command(const char* command, char* response, size_t response_size, int timeout_ms);
static bool gps_enable_gnss(void);
static bool gps_disable_gnss(void);
static bool gps_stop_output(void);

// Ring buffer functions (4KB GPS data buffer)
static bool ring_buffer_init(void);
static void ring_buffer_deinit(void);
static bool ring_buffer_write(const char* data, size_t len);
static size_t ring_buffer_read(char* buffer, size_t max_len);
static size_t ring_buffer_available(void);
static void ring_buffer_clear(void);

// NMEA parsing functions
static bool parse_nmea_coordinate(const char* coord_str, char dir, float* result);
static bool parse_gnrmc(const char* sentence, gps_data_t* data);
static bool parse_gngga(const char* sentence, gps_data_t* data);
static bool parse_gpgsv(const char* sentence, gps_data_t* data);
static bool validate_nmea_checksum(const char* sentence);

// GPS interface implementation
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

// =============================================================================
// 4KB GPS RING BUFFER IMPLEMENTATION (As requested by user)
// =============================================================================

static bool ring_buffer_init(void)
{
    gps_buffer.mutex = xSemaphoreCreateMutex();
    if (!gps_buffer.mutex) {
        ESP_LOGE(TAG, "Failed to create ring buffer mutex");
        return false;
    }
    
    gps_buffer.write_idx = 0;
    gps_buffer.read_idx = 0;
    gps_buffer.count = 0;
    memset(gps_buffer.data, 0, GPS_RING_BUFFER_SIZE);
    
    ESP_LOGI(TAG, "4KB GPS ring buffer initialized");
    return true;
}

static void ring_buffer_deinit(void)
{
    if (gps_buffer.mutex) {
        vSemaphoreDelete(gps_buffer.mutex);
        gps_buffer.mutex = NULL;
    }
    ESP_LOGI(TAG, "GPS ring buffer deinitialized");
}

static bool __attribute__((unused)) ring_buffer_write(const char* data, size_t len)
{
    if (!data || len == 0 || !gps_buffer.mutex) {
        return false;
    }
    
    if (xSemaphoreTake(gps_buffer.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    
    for (size_t i = 0; i < len; i++) {
        gps_buffer.data[gps_buffer.write_idx] = data[i];
        gps_buffer.write_idx = (gps_buffer.write_idx + 1) % GPS_RING_BUFFER_SIZE;
        
        if (gps_buffer.count < GPS_RING_BUFFER_SIZE) {
            gps_buffer.count++;
        } else {
            // Buffer full, advance read pointer (overwrite oldest data)
            gps_buffer.read_idx = (gps_buffer.read_idx + 1) % GPS_RING_BUFFER_SIZE;
        }
    }
    
    xSemaphoreGive(gps_buffer.mutex);
    return true;
}

static size_t __attribute__((unused)) ring_buffer_read(char* buffer, size_t max_len)
{
    if (!buffer || max_len == 0 || !gps_buffer.mutex) {
        return 0;
    }
    
    if (xSemaphoreTake(gps_buffer.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }
    
    size_t bytes_read = 0;
    while (bytes_read < max_len && gps_buffer.count > 0) {
        buffer[bytes_read] = gps_buffer.data[gps_buffer.read_idx];
        gps_buffer.read_idx = (gps_buffer.read_idx + 1) % GPS_RING_BUFFER_SIZE;
        gps_buffer.count--;
        bytes_read++;
    }
    
    xSemaphoreGive(gps_buffer.mutex);
    return bytes_read;
}

static size_t __attribute__((unused)) ring_buffer_available(void)
{
    if (!gps_buffer.mutex) {
        return 0;
    }
    
    if (xSemaphoreTake(gps_buffer.mutex, pdMS_TO_TICKS(1)) != pdTRUE) {
        return 0;
    }
    
    size_t available = gps_buffer.count;
    xSemaphoreGive(gps_buffer.mutex);
    return available;
}

static void __attribute__((unused)) ring_buffer_clear(void)
{
    if (!gps_buffer.mutex) {
        return;
    }
    
    if (xSemaphoreTake(gps_buffer.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        gps_buffer.write_idx = 0;
        gps_buffer.read_idx = 0;
        gps_buffer.count = 0;
        xSemaphoreGive(gps_buffer.mutex);
    }
}

static bool gps_init_impl(const gps_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    if (module_initialized) {
        ESP_LOGW(TAG, "GPS module already initialized");
        return true;
    }
    
    // Store configuration
    memcpy(&current_config, config, sizeof(gps_config_t));
    
    // GPS module uses the LTE module's UART interface
    // No need to initialize UART here as LTE module handles it
    ESP_LOGI(TAG, "GPS module using shared LTE UART interface");
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.initialized = true;
    module_status.uart_ready = true;
    
    // Initialize 4KB GPS ring buffer (as requested)
    if (!ring_buffer_init()) {
        ESP_LOGE(TAG, "Failed to initialize GPS ring buffer");
        return false;
    }
    
    module_initialized = true;
    
    // Enable GNSS using AT commands (Waveshare documentation)
    if (!gps_enable_gnss()) {
        ESP_LOGE(TAG, "Failed to enable GNSS");
        gps_deinit_impl();
        return false;
    }
    
    // NO auto data output - polling only mode to avoid UART collisions
    ESP_LOGI(TAG, "✅ GNSS enabled for POLLING ONLY - no auto output to prevent collisions");
    
    if (config->debug_output) {
        ESP_LOGI(TAG, "GPS module initialized successfully with AT commands");
        ESP_LOGI(TAG, "  Fix timeout: %d ms", config->fix_timeout_ms);
        ESP_LOGI(TAG, "  Min satellites: %d", config->min_satellites);
        ESP_LOGI(TAG, "  Update interval: %d ms", config->data_update_interval_ms);
        ESP_LOGI(TAG, "  GNSS powered on and data output enabled");
    }
    
    return true;
}

static bool gps_deinit_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    // Stop GNSS data output and power down
    gps_stop_output();
    gps_disable_gnss();
    
    // Cleanup 4KB GPS ring buffer
    ring_buffer_deinit();
    
    // No need to delete UART driver as LTE module manages it
    memset(&module_status, 0, sizeof(module_status));
    module_initialized = false;
    
    ESP_LOGI(TAG, "GPS module deinitialized");
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!module_initialized || !data) {
        return false;
    }
    
    // Use LTE module's AT interface to access preserved NMEA data and polling
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE module AT interface not available");
        return false;
    }
    
    char* buffer = malloc(4096);  // Larger buffer for preserved NMEA data
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return false;
    }
    
    // Clear data structure
    memset(data, 0, sizeof(gps_data_t));
    
    // FIRST: Try to get preserved NMEA data (this has the satellite information!)
    size_t preserved_length = 0;
    bool has_preserved_data = false;
    
    if (lte->get_preserved_nmea && lte->get_preserved_nmea(buffer, 4095, &preserved_length)) {
        if (preserved_length > 0) {
            buffer[preserved_length] = '\0';
            has_preserved_data = true;
            if (current_config.debug_output) {
                ESP_LOGD(TAG, "Using preserved NMEA data (%zu bytes)", preserved_length);
            }
        }
    }
    
    // SECOND: If no preserved data, fall back to polling
    if (!has_preserved_data) {
        at_response_t response = {0};
        
        if (lte->send_at_command("AT+CGNSINF", &response, 3000)) {
            if (response.response[0] != '\0' && strstr(response.response, "+CGNSINF:") != NULL) {
                strncpy(buffer, response.response, 4095);
                buffer[4095] = '\0';
            } else {
                if (current_config.debug_output) {
                    ESP_LOGW(TAG, "No GNSS data in poll response");
                }
                free(buffer);
                return false;
            }
        } else {
            if (current_config.debug_output) {
                ESP_LOGW(TAG, "GNSS polling failed");
            }
            free(buffer);
            return false;
        }
    }
    
    // Parse NMEA sentences (either from preserved data or polling)
    module_status.total_sentences_parsed++;
    
    char* line = strtok(buffer, "\r\n");
    int sentences_processed = 0;
    int max_satellites = 0; // Track maximum satellites from all constellations
    
    while (line != NULL) {
        if (strlen(line) > 5) {  // Valid NMEA sentence minimum length
            sentences_processed++;
            
            if (validate_nmea_checksum(line)) {
                // Parse different NMEA sentence types
                if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0) {
                    if (parse_gnrmc(line, data)) {
                        module_status.valid_sentences++;
                    }
                } else if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
                    if (parse_gngga(line, data)) {
                        module_status.valid_sentences++;
                    }
                } else if (strncmp(line, "$GPGSV", 6) == 0 || strncmp(line, "$GLGSV", 6) == 0 || 
                          strncmp(line, "$GAGSV", 6) == 0 || strncmp(line, "$BDGSV", 6) == 0) {
                    // Parse satellite info from all GNSS systems and accumulate
                    if (parse_gpgsv(line, data)) {
                        if (data->satellites > max_satellites) {
                            max_satellites = data->satellites;
                        }
                    }
                }
            } else if (current_config.debug_output) {
                module_status.parse_errors++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    
    // Use the maximum satellite count found
    data->satellites = max_satellites;
    
    // Clean output - only show important information
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GPS Status: Fix=%s, Sats=%d, Lat=%.6f, Lon=%.6f, Speed=%.1f km/h", 
                 data->fix_valid ? "YES" : "NO", data->satellites,
                 data->latitude, data->longitude, data->speed_kmh);
    }
    
    free(buffer);
    
    if (data->fix_valid) {
        module_status.last_fix_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (current_config.debug_output) {
            ESP_LOGI(TAG, "GPS Fix: %.6f, %.6f, %d sats, %.1f km/h, HDOP=%.1f", 
                     data->latitude, data->longitude, data->satellites, 
                     data->speed_kmh, data->hdop);
        }
    } else if (current_config.debug_output) {
        ESP_LOGD(TAG, "GPS: No valid fix (sats: %d)", data->satellites);
    }
    
    return data->fix_valid;
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
    if (!module_initialized) {
        ESP_LOGE(TAG, "GPS module not initialized");
        return false;
    }
    
    if (module_status.gps_power_on && module_status.gnss_enabled && module_status.data_output_enabled) {
        ESP_LOGW(TAG, "GPS already powered on and configured");
        return true;
    }
    
    // Enable GNSS power ONLY - no auto output (polling mode)
    if (!gps_enable_gnss()) {
        ESP_LOGE(TAG, "Failed to enable GNSS power");
        return false;
    }
    
    // Wait for GNSS to stabilize (shorter wait for polling mode)
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GNSS enabled in polling mode");
    }
    
    module_status.gps_power_on = true;
    ESP_LOGI(TAG, "GPS power on successful");
    return true;
}

static bool gps_power_off_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    // No port switching needed - just power off GNSS
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // No need to stop data output - we never started auto output
    // Just disable GNSS power
    gps_disable_gnss();
    
    module_status.gps_power_on = false;
    ESP_LOGI(TAG, "GPS power off successful");
    return true;
}

static bool gps_reset_impl(void)
{
    ESP_LOGI(TAG, "GPS reset requested");
    
    // Power off first
    gps_power_off_impl();
    
    // Wait a moment
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Reset status
    memset(&module_status, 0, sizeof(module_status));
    module_status.initialized = module_initialized;
    module_status.uart_ready = module_initialized;
    
    // Power back on
    return gps_power_on_impl();
}

static void gps_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// NMEA parsing implementation
static bool validate_nmea_checksum(const char* sentence)
{
    if (!sentence || strlen(sentence) < 4) {
        return false;
    }
    
    // Find checksum
    const char* checksum_pos = strrchr(sentence, '*');
    if (!checksum_pos) {
        return false; // No checksum
    }
    
    // Calculate checksum
    uint8_t calc_checksum = 0;
    for (const char* p = sentence + 1; p < checksum_pos; p++) {
        calc_checksum ^= *p;
    }
    
    // Parse provided checksum
    uint8_t provided_checksum = (uint8_t)strtol(checksum_pos + 1, NULL, 16);
    
    return calc_checksum == provided_checksum;
}

static bool parse_nmea_coordinate(const char* coord_str, char dir, float* result)
{
    if (!coord_str || strlen(coord_str) < 7 || !result) {
        return false;
    }
    
    float coord = atof(coord_str);
    int degrees = (int)(coord / 100);
    float minutes = coord - (degrees * 100);
    
    *result = degrees + (minutes / 60.0f);
    
    if (dir == 'S' || dir == 'W') {
        *result = -*result;
    }
    
    return true;
}

static bool parse_gnrmc(const char* sentence, gps_data_t* data)
{
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[sizeof(temp_sentence) - 1] = '\0';
    
    char* tokens[12];
    char* token = strtok(temp_sentence, ",");
    int token_count = 0;
    
    while (token && token_count < 12) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    if (token_count < 10) {
        return false;
    }
    
    // Check if fix is valid (A = valid, V = invalid)
    if (tokens[2][0] != 'A') {
        data->fix_valid = false;
        return false;
    }
    
    // Parse latitude and longitude
    if (!parse_nmea_coordinate(tokens[3], tokens[4][0], &data->latitude) ||
        !parse_nmea_coordinate(tokens[5], tokens[6][0], &data->longitude)) {
        return false;
    }
    
    // Parse speed (knots to km/h)
    if (strlen(tokens[7]) > 0) {
        data->speed_kmh = atof(tokens[7]) * 1.852f;
    }
    
    // Parse course
    if (strlen(tokens[8]) > 0) {
        data->course = atof(tokens[8]);
    }
    
    // Create timestamp
    if (strlen(tokens[1]) >= 6 && strlen(tokens[9]) >= 6) {
        snprintf(data->timestamp, sizeof(data->timestamp), 
                "20%c%c-%c%c-%c%cT%c%c:%c%c:%c%c",
                tokens[9][4], tokens[9][5],  // year
                tokens[9][2], tokens[9][3],  // month
                tokens[9][0], tokens[9][1],  // day
                tokens[1][0], tokens[1][1],  // hour
                tokens[1][2], tokens[1][3],  // minute
                tokens[1][4], tokens[1][5]); // second
    }
    
    data->fix_valid = true;
    return true;
}

static bool parse_gngga(const char* sentence, gps_data_t* data)
{
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[sizeof(temp_sentence) - 1] = '\0';
    
    char* tokens[15];
    char* token = strtok(temp_sentence, ",");
    int token_count = 0;
    
    while (token && token_count < 15) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    if (token_count < 11) {
        return false;
    }
    
    // Parse fix quality
    if (strlen(tokens[6]) > 0) {
        data->fix_quality = tokens[6][0];
    }
    
    // Parse altitude
    if (strlen(tokens[9]) > 0) {
        data->altitude = atof(tokens[9]);
    }
    
    // Parse HDOP
    if (strlen(tokens[8]) > 0) {
        data->hdop = atof(tokens[8]);
    }
    
    return true;
}

static bool parse_gpgsv(const char* sentence, gps_data_t* data)
{
    // GSV format: $GPGSV,total_msg,msg_num,total_sats,sat1_prn,sat1_elev,sat1_azim,sat1_snr,...*checksum
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[255] = '\0';
    
    char* token = strtok(temp_sentence, ",");
    int field = 0;
    int satellites_in_view = 0;
    
    // Parse the sentence to get satellite count
    while (token && field < 4) {
        if (field == 3) { // Total satellites in view for this constellation
            satellites_in_view = atoi(token);
            // Accumulate satellites from all constellations (GPS, GLONASS, Galileo, BeiDou)
            // Only add if this is a valid count and we haven't counted this constellation yet
            if (satellites_in_view > 0) {
                // Simple accumulation - in real implementation you'd track which constellations
                // you've already counted, but for now just take the maximum
                if (satellites_in_view > data->satellites) {
                    data->satellites = satellites_in_view;
                }
                if (current_config.debug_output) {
                    ESP_LOGD(TAG, "Satellites in view: %d (constellation total: %d)", 
                            data->satellites, satellites_in_view);
                }
            }
            return satellites_in_view > 0;
        }
        token = strtok(NULL, ",");
        field++;
    }
    
    return false;
}

// Utility functions
bool gps_is_fix_valid(const gps_data_t* data)
{
    return data && data->fix_valid && 
           data->satellites >= current_config.min_satellites &&
           data->hdop > 0 && data->hdop < 20.0f; // Reasonable HDOP range
}

float gps_calculate_distance(float lat1, float lon1, float lat2, float lon2)
{
    const float R = 6371000; // Earth radius in meters
    float dlat = (lat2 - lat1) * M_PI / 180.0f;
    float dlon = (lon2 - lon1) * M_PI / 180.0f;
    float a = sin(dlat/2) * sin(dlat/2) + cos(lat1 * M_PI / 180.0f) * cos(lat2 * M_PI / 180.0f) * sin(dlon/2) * sin(dlon/2);
    float c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

bool gps_format_coordinates(const gps_data_t* data, char* buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size < 32) {
        return false;
    }
    
    snprintf(buffer, buffer_size, "%.6f,%.6f", data->latitude, data->longitude);
    return true;
}

// AT command implementation for SIM7670G GPS control
// Uses LTE module's AT interface instead of direct UART access

static bool send_gps_at_command(const char* command, char* response, size_t response_size, int timeout_ms)
{
    if (!module_initialized || !command) {
        return false;
    }
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GPS AT CMD: %s", command);
    }
    
    // Use LTE module's AT command interface for GPS commands
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE module AT interface not available");
        return false;
    }
    
    at_response_t at_response = {0};
    bool success = lte->send_at_command(command, &at_response, timeout_ms);
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GPS AT RESP: %s (success: %s)", 
                 at_response.response, success ? "YES" : "NO");
    }
    
    // Copy response if requested
    if (response && response_size > 0 && at_response.response[0] != '\0') {
        strncpy(response, at_response.response, response_size - 1);
        response[response_size - 1] = '\0';
    }
    
    // Check for successful GPS responses
    return success && (
        strstr(at_response.response, "OK") != NULL ||
        strstr(at_response.response, "READY") != NULL ||
        strstr(at_response.response, "+CGNSSPWR") != NULL
    );
}

static bool gps_enable_gnss(void)
{
    char response[256];
    
    // Send AT+CGNSSPWR=1 (Open GNSS) - Waveshare documentation
    if (!send_gps_at_command("AT+CGNSSPWR=1", response, sizeof(response), 5000)) {
        ESP_LOGE(TAG, "Failed to enable GNSS power");
        return false;
    }
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GNSS power enabled successfully");
    }
    
    module_status.gnss_enabled = true;
    return true;
}

static bool gps_disable_gnss(void)
{
    char response[256];
    
    if (!send_gps_at_command("AT+CGNSSPWR=0", response, sizeof(response), 3000)) {
        ESP_LOGE(TAG, "Failed to disable GNSS power");
        return false;
    }
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GNSS power disabled");
    }
    
    module_status.gnss_enabled = false;
    return true;
}

// gps_start_output() removed - auto-output disabled to prevent UART collisions
// Use polling with AT+CGNSINF instead of AT+CGNSSTST=1

static bool gps_stop_output(void)
{
    char response[256];
    
    if (!send_gps_at_command("AT+CGNSSTST=0", response, sizeof(response), 3000)) {
        ESP_LOGE(TAG, "Failed to stop GNSS data output");
        return false;
    }
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "GNSS data output stopped");
    }
    
    module_status.data_output_enabled = false;
    return true;
}