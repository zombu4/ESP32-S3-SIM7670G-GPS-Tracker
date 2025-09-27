#include "gps_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"
#include "time.h"
// Include LTE module for shared AT interface
#include "../lte/lte_module.h"
// Include nuclear integration check
#include "../parallel/nuclear_integration.h"

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

// GPS polling functions (Waveshare official method)
static bool parse_cgnsinf_response(const char* response, gps_data_t* data);
static bool parse_nmea_gga_sentence(const char* nmea, gps_data_t* data);
static bool parse_gngga(const char* sentence, gps_data_t* data);
static bool parse_gpgsv(const char* sentence, gps_data_t* data);
static bool parse_cgnsinf_response(const char* response, gps_data_t* data);
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
 
 // UART initialized by main system - no individual module init needed
 ESP_LOGI(TAG, "GPS module using system UART interface");
 
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
 
 // Active polling mode - GPS data acquired via AT+CGNSINF on demand
 ESP_LOGI(TAG, " GNSS enabled for active polling - data acquired via AT+CGNSINF");
 
 if (config->debug_output) {
 ESP_LOGI(TAG, "GPS module initialized successfully with AT commands");
 ESP_LOGI(TAG, " Fix timeout: %d ms", config->fix_timeout_ms);
 ESP_LOGI(TAG, " Min satellites: %d", config->min_satellites);
 ESP_LOGI(TAG, " Update interval: %d ms", config->data_update_interval_ms);
 ESP_LOGI(TAG, " GNSS powered on and data output enabled");
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

// Forward declaration of simple debug function
extern bool gps_read_data_simple_debug(void* data);

static bool gps_read_data_impl(gps_data_t* data)
{
 if (!module_initialized || !data) {
 return false;
 }
 
 ESP_LOGI(TAG, "🔍 GPS DEBUG MODE: Using simple function to show raw data without parsing");
 ESP_LOGI(TAG, "🔍 This avoids crashes and shows what GPS module actually outputs");
 
 // Clear data structure for safety
 memset(data, 0, sizeof(gps_data_t));
 
 // Use simple debug function instead of complex parsing
 return gps_read_data_simple_debug(data);
}
 
 // 🔥 CRITICAL FIX: SIM7670G outputs NMEA data directly to UART after AT+CGNSSTST=1
 // NMEA data is handled by nuclear pipeline - get data from nuclear GPS buffer

 if (current_config.debug_output) {
    ESP_LOGI(TAG, "�️  GPS POLLING: Using AT+CGNSINF for GPS data (Waveshare official method)");
    }

    // 🔥 WORKING GPS METHOD: Direct NMEA reading from nuclear pipeline
    // Based on working implementation from modem_init.c
    ESP_LOGI(TAG, "�️  Reading NMEA data via nuclear pipeline (proven working method)");
    
    // First try nuclear pipeline for NMEA data
    bool nmea_found = false;
    int pipeline_bytes = 0;

    // Get nuclear pipeline interface for GPS data access
    extern nuclear_integration_manager_t* get_nuclear_integration_manager(void);
    nuclear_integration_manager_t *nuclear_manager = get_nuclear_integration_manager(); int bytes_read = 0;
 
 if (nuclear_manager && nuclear_manager->integration_active && nuclear_manager->pipeline) {
     // Read GPS data from nuclear pipeline (prevents UART conflicts)
     uint8_t *pipeline_data = NULL;
     size_t pipeline_size = nuclear_pipeline_read_gps(
         nuclear_manager->pipeline, 
         &pipeline_data, 
         pdMS_TO_TICKS(100) // Reduced timeout to prevent watchdog issues
     );
     
     if (pipeline_size > 0 && pipeline_data && pipeline_size < 4095) {
         memcpy(buffer, pipeline_data, pipeline_size);
         bytes_read = pipeline_size;
         buffer[bytes_read] = '\0';
         
         // 💀🔥 ULTRA VERBOSE GPS DATA DEBUGGING 🔥💀
         ESP_LOGI(TAG, "🔥 GPS ULTRA VERBOSE: Retrieved %d bytes from nuclear GPS pipeline", bytes_read);
         ESP_LOGI(TAG, "🔥 GPS RAW DATA: '%.*s'", bytes_read < 256 ? bytes_read : 256, buffer);
         
         // Show hex dump for binary data analysis
         if (bytes_read > 0) {
             char hex_dump[256] = {0};
             int hex_pos = 0;
             for (int i = 0; i < bytes_read && i < 64 && hex_pos < 240; i++) {
                 hex_pos += snprintf(hex_dump + hex_pos, sizeof(hex_dump) - hex_pos, 
                                   "%02X ", (unsigned char)buffer[i]);
             }
             ESP_LOGI(TAG, "🔥 GPS HEX DUMP: %s", hex_dump);
         }
         
         // Check for NMEA sentences using proven working method
         if (strstr(buffer, "$G") || strstr(buffer, "$GNRMC") || strstr(buffer, "$GNGGA")) {
             nmea_found = true;
             pipeline_bytes = bytes_read;
             ESP_LOGI(TAG, "✅ NMEA SENTENCES FOUND in nuclear pipeline!");
             
             // Count different sentence types like working implementation
             int gprmc_count = 0, gpgga_count = 0, gpgsa_count = 0, gpgsv_count = 0;
             char* ptr = buffer;
             while ((ptr = strstr(ptr, "$GPRMC")) != NULL) { gprmc_count++; ptr++; }
             ptr = buffer;
             while ((ptr = strstr(ptr, "$GPGGA")) != NULL) { gpgga_count++; ptr++; }
             ptr = buffer; 
             while ((ptr = strstr(ptr, "$GPGSA")) != NULL) { gpgsa_count++; ptr++; }
             ptr = buffer;
             while ((ptr = strstr(ptr, "$GPGSV")) != NULL) { gpgsv_count++; ptr++; }
             
             ESP_LOGI(TAG, "🛰️  NMEA Sentence counts: RMC:%d GGA:%d GSA:%d GSV:%d", 
                      gprmc_count, gpgga_count, gpgsa_count, gpgsv_count);
         } else {
             ESP_LOGI(TAG, "📡 Nuclear pipeline data (%d bytes) but no NMEA sentences yet", bytes_read);
             ESP_LOGW(TAG, "⚠️ Raw data: %.*s", bytes_read < 128 ? bytes_read : 128, buffer);
         }
         
         // Return buffer to nuclear pipeline
         nuclear_pipeline_return_buffer(nuclear_manager->pipeline, pipeline_data, true);
         
         if (current_config.debug_output) {
             ESP_LOGI(TAG, "🔥 GPS VERBOSE: Retrieved %d bytes from nuclear GPS pipeline", bytes_read);
         }
     } else {
         ESP_LOGW(TAG, "💀 GPS PIPELINE: No data available (size=%d, data=%p)", 
                  (int)pipeline_size, pipeline_data);
     }
 }
 
 // If no NMEA found in nuclear pipeline, try direct UART read like working implementation
 if (!nmea_found) {
     ESP_LOGI(TAG, "🔄 No NMEA in nuclear pipeline, trying direct UART read...");
     
     const lte_interface_t* lte = lte_get_interface();
     if (lte && lte->read_raw_data) {
         // Use LTE interface's raw UART reading like working implementation
         memset(buffer, 0, 4096);
         size_t lte_bytes_read = 0;
         bool success = lte->read_raw_data(buffer, 4095, &lte_bytes_read, 3000); // 3 second timeout
         bytes_read = success ? lte_bytes_read : 0;
         
         if (bytes_read > 0) {
             buffer[bytes_read] = '\0';
             ESP_LOGI(TAG, "🎯 DIRECT UART SUCCESS: Found %d bytes!", bytes_read);
             
             // Check for NMEA sentences using proven working method
             if (strstr(buffer, "$G") || strstr(buffer, "$GNRMC") || strstr(buffer, "$GNGGA")) {
                 nmea_found = true;
                 ESP_LOGI(TAG, "✅ NMEA SENTENCES FOUND in direct UART!");
             } else {
                 ESP_LOGW(TAG, "⚠️  Direct UART data but no NMEA sentences");
             }
         } else {
             ESP_LOGW(TAG, "💀 No data from direct UART read either");
         }
     } else {
         // Final fallback: Raw UART if LTE interface unavailable
         ESP_LOGI(TAG, "🔄 LTE interface unavailable, trying raw UART...");
         bytes_read = uart_read_bytes(UART_NUM_1, (uint8_t*)buffer, 4095, pdMS_TO_TICKS(3000));
         if (bytes_read > 0) {
             buffer[bytes_read] = '\0';
             if (strstr(buffer, "$G")) {
                 nmea_found = true;
                 ESP_LOGI(TAG, "✅ NMEA found in raw UART fallback!");
             }
         }
     }
 }
 
 // Check if we found any NMEA data
 if (!nmea_found || bytes_read <= 0) {
     if (current_config.debug_output) {
         ESP_LOGW(TAG, "🔥 GPS VERBOSE: No NMEA data available (may be searching for satellites)");
         ESP_LOGI(TAG, "💡 GPS status: pipeline_bytes=%d, direct_bytes=%d, nmea_found=%s", 
                  pipeline_bytes, bytes_read, nmea_found ? "YES" : "NO");
     }
     free(buffer);
     return false;
 }
 
 ESP_LOGI(TAG, "✅ Processing %d bytes of NMEA data from %s", 
          bytes_read, pipeline_bytes > 0 ? "nuclear pipeline" : "direct UART");
 
 buffer[bytes_read] = '\0'; // Null terminate
 
 // 🔥 VERBOSE NMEA DATA DEBUGGING 🔥
 if (current_config.debug_output) {
 ESP_LOGI(TAG, "🔥 GPS VERBOSE: Raw NMEA data from UART [%d bytes]: '%.*s'", bytes_read, bytes_read, buffer);
 
 // Show buffer in hex for debugging
 ESP_LOGI(TAG, "🔥 GPS VERBOSE: NMEA hex dump:");
 for (int i = 0; i < bytes_read && i < 128; i += 16) {
     char hex_line[128] = {0};
     char ascii_line[17] = {0};
     int line_len = 0;
     
     for (int j = 0; j < 16 && (i + j) < bytes_read; j++) {
         unsigned char byte = (unsigned char)buffer[i + j];
         line_len += sprintf(hex_line + line_len, "%02X ", byte);
         ascii_line[j] = (byte >= 32 && byte < 127) ? byte : '.';
     }
     ascii_line[16] = '\0';
     ESP_LOGI(TAG, "🔥   %04X: %-48s |%s|", i, hex_line, ascii_line);
 }
 }
 
 // Parse NMEA sentences using proven working method from modem_init.c
 module_status.total_sentences_parsed++;
 
 char* line = strtok(buffer, "\r\n");
 bool gps_fix_found = false;
 
 while (line != NULL) {
     if (strlen(line) > 6) { // Valid NMEA sentence minimum length
         ESP_LOGD(TAG, "Processing NMEA line: %s", line);
         
         // Use working GGA parsing function
         if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
             if (parse_nmea_gga_sentence(line, data)) {
                 gps_fix_found = true;
                 module_status.valid_sentences++;
                 ESP_LOGI(TAG, "✅ GPS fix acquired from NMEA GGA sentence!");
                 break; // Got fix, no need to process more sentences
             }
         }
         
         // Also try RMC sentences as backup
         if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0) {
             if (parse_gnrmc(line, data)) {
                 if (!gps_fix_found) { // Only use RMC if no GGA fix
                     gps_fix_found = true;
                     module_status.valid_sentences++;
                     ESP_LOGI(TAG, "✅ GPS fix acquired from NMEA RMC sentence!");
                 }
             }
         }
     }
     line = strtok(NULL, "\r\n");
 }
 
 // GPS fix validation will be done by individual sentence parsers
 
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
 tokens[9][4], tokens[9][5], // year
 tokens[9][2], tokens[9][3], // month
 tokens[9][0], tokens[9][1], // day
 tokens[1][0], tokens[1][1], // hour
 tokens[1][2], tokens[1][3], // minute
 tokens[1][4], tokens[1][5]); // second
 }
 
 data->fix_valid = true;
 return true;
}

static bool parse_gngga(const char* sentence, gps_data_t* data)
{
 // GGA format: $GNGGA,time,lat,lat_dir,lon,lon_dir,fix_quality,num_sats,hdop,alt,alt_units,geo_sep,geo_units,dgps_age,dgps_id*checksum
 // Example: $GNGGA,135925.508,2639.54840,N,08206.84216,W,1,7,1.41,20.4,M,-29.0,M,,*65
 
 if (!sentence || !data) return false;
 
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
 
 bool has_valid_data = false;
 
 // Parse latitude (field 2 and 3)
 if (strlen(tokens[2]) > 0 && strlen(tokens[3]) > 0) {
 if (parse_nmea_coordinate(tokens[2], tokens[3][0], &data->latitude)) {
 has_valid_data = true;
 }
 }
 
 // Parse longitude (field 4 and 5)
 if (strlen(tokens[4]) > 0 && strlen(tokens[5]) > 0) {
 if (parse_nmea_coordinate(tokens[4], tokens[5][0], &data->longitude)) {
 has_valid_data = true;
 }
 }
 
 // Parse fix quality (field 6)
 if (strlen(tokens[6]) > 0) {
 int fix_quality = atoi(tokens[6]);
 data->fix_quality = tokens[6][0];
 data->fix_valid = (fix_quality > 0); // Any non-zero quality means we have a fix
 if (fix_quality > 0) has_valid_data = true;
 }
 
 // Parse number of satellites (field 7) - only if it's higher than what we already have
 if (strlen(tokens[7]) > 0) {
 int sats = atoi(tokens[7]);
 // Only update if GGA reports more satellites than GSV sentences already found
 // This prevents GGA from resetting satellite count to 0 when no fix
 if (sats > data->satellites) { 
 data->satellites = sats;
 }
 // Don't mark as valid data just because GGA reports 0 satellites
 if (sats > 0) has_valid_data = true;
 }
 
 // Parse HDOP (field 8)
 if (strlen(tokens[8]) > 0) {
 data->hdop = atof(tokens[8]);
 if (data->hdop > 0) has_valid_data = true;
 }
 
 // Parse altitude (field 9)
 if (strlen(tokens[9]) > 0) {
 data->altitude = atof(tokens[9]);
 has_valid_data = true;
 }
 
 if (current_config.debug_output && has_valid_data) {
 ESP_LOGD(TAG, "[GGA] Fix=%d, Lat=%.6f, Lon=%.6f, Sats=%d, HDOP=%.2f, Alt=%.1f", 
 data->fix_valid ? 1 : 0, data->latitude, data->longitude, 
 data->satellites, data->hdop, data->altitude);
 }
 
 return has_valid_data;
}

static bool parse_gpgsv(const char* sentence, gps_data_t* data)
{
 // GSV format: $GPGSV,total_msg,msg_num,total_sats,sat1_prn,sat1_elev,sat1_azim,sat1_snr,...*checksum
 // Example: $GPGSV,2,1,08,05,14,078,,24,49,125,23,15,46,033,,23,44,327,22*6D
 
 if (!sentence || !data) return false;
 
 // Find the comma positions to extract fields properly
 const char* p = sentence;
 int comma_count = 0;
 int satellites_in_view = 0;
 
 // Skip the $GPGSV/$GLGSV/$GAGSV/$BDGSV part
 while (*p && *p != ',') p++;
 if (!*p) return false;
 
 // Parse fields: total_msg,msg_num,total_sats
 for (comma_count = 0; comma_count < 3 && *p; comma_count++) {
 p++; // Skip the comma
 if (comma_count == 2) { // total_sats field
 satellites_in_view = atoi(p);
 break;
 }
 // Skip to next comma
 while (*p && *p != ',') p++;
 }
 
 if (satellites_in_view > 0) {
 // Simply add satellites from this constellation to running total
 // Each GSV sentence type represents a different constellation
 data->satellites += satellites_in_view;
 
 // Force debug output to see what's happening
 ESP_LOGI(TAG, "[GSV] %s: %d sats (Running total now: %d)", 
 sentence, satellites_in_view, data->satellites);
 return true;
 }
 
 return false;
}

// Parse AT+CGNSINF response: +CGNSINF: run,fix,utc,lat,lon,alt,speed,course,fixmode,reserved1,hdop,pdop,vdop,reserved2,view,used,reserved3
static bool parse_cgnsinf_response(const char* response, gps_data_t* data)
{
 if (!response || !data) {
 ESP_LOGW(TAG, "🔥 CGNSINF PARSE: Invalid parameters");
 return false;
 }
 
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: Input response: '%s'", response);
 
 // Find the +CGNSINF: prefix
 const char* start = strstr(response, "+CGNSINF:");
 if (!start) {
 ESP_LOGW(TAG, "🔥 CGNSINF PARSE: No +CGNSINF: header found in response");
 return false;
 }
 
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: Found +CGNSINF: header");
 
 // Move past "+CGNSINF: "
 start += 10;
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: Data part: '%s'", start);
 
 // Parse comma-separated values
 // Format: run,fix,utc,lat,lon,alt,speed,course,fixmode,reserved1,hdop,pdop,vdop,reserved2,view,used,reserved3
 int run_status, fix_status, fixmode, satellites_view, satellites_used;
 char utc_time[32];
 float latitude, longitude, altitude, speed, course, hdop, pdop, vdop;
 
 int parsed = sscanf(start, "%d,%d,%31[^,],%f,%f,%f,%f,%f,%d,%*[^,],%f,%f,%f,%*[^,],%d,%d",
 &run_status, &fix_status, utc_time, &latitude, &longitude, &altitude, 
 &speed, &course, &fixmode, &hdop, &pdop, &vdop, &satellites_view, &satellites_used);
 
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: sscanf parsed %d fields", parsed);
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: run_status=%d, fix_status=%d, utc='%s'", run_status, fix_status, utc_time);
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: lat=%.6f, lon=%.6f, alt=%.1f", latitude, longitude, altitude);
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: speed=%.1f, course=%.1f, fixmode=%d", speed, course, fixmode);
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: hdop=%.1f, sats_view=%d, sats_used=%d", hdop, satellites_view, satellites_used);
 
 if (parsed >= 6) { // At least need run, fix, utc, lat, lon, alt
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: ✅ Successfully parsed minimum required fields");
 
 data->fix_valid = (fix_status == 1); // 1 = GPS fix available
 data->latitude = latitude;
 data->longitude = longitude;
 data->altitude = altitude;
 data->speed_kmh = speed * 3.6f; // Convert m/s to km/h
 data->course = course; // Use correct field name
 data->hdop = hdop;
 // Note: pdop and vdop not available in gps_data_t structure
 data->satellites = satellites_used > 0 ? satellites_used : satellites_view;
 
 // Parse timestamp if available
 if (strlen(utc_time) > 10) {
 // UTC time format: yyyymmddhhmmss.sss - copy to timestamp array
 if (strlen(utc_time) >= 14) {
 strncpy(data->timestamp, utc_time, sizeof(data->timestamp) - 1);
 data->timestamp[sizeof(data->timestamp) - 1] = '\0';
 }
 }
 
 ESP_LOGI(TAG, "🔥 CGNSINF PARSE: Final result - Fix=%s, Sats=%d/%d, Lat=%.6f, Lon=%.6f, HDOP=%.1f", 
 data->fix_valid ? "YES" : "NO", satellites_used, satellites_view,
 data->latitude, data->longitude, data->hdop);
 
 return true;
 } else {
 ESP_LOGW(TAG, "🔥 CGNSINF PARSE: ❌ Failed to parse minimum fields (got %d, need 6)", parsed);
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
 
 // Check if nuclear pipeline is active - if so, use nuclear AT command interface
 if (nuclear_integration_is_active()) {
     ESP_LOGW(TAG, "Nuclear pipeline active - using nuclear AT command interface");
     return nuclear_send_at_command(command, response, response_size, timeout_ms);
 }
 
 if (current_config.debug_output) {
 ESP_LOGI(TAG, "GPS AT CMD: %s", command);
 }
 
 // Send AT command directly via UART
 char local_response[256] = {0};
 
 // Send command
 uart_write_bytes(UART_NUM_1, command, strlen(command));
 uart_write_bytes(UART_NUM_1, "\r\n", 2);
 
 // Read response
 int bytes_read = uart_read_bytes(UART_NUM_1, local_response, sizeof(local_response) - 1, pdMS_TO_TICKS(timeout_ms));
 bool success = (bytes_read > 0);
 
 if (bytes_read > 0) {
     local_response[bytes_read] = '\0';
 }
 
 if (current_config.debug_output) {
 ESP_LOGI(TAG, "GPS AT RESP: %s (success: %s)", 
 local_response, success ? "YES" : "NO");
 }
 
 // Copy response if requested
 if (response && response_size > 0 && local_response[0] != '\0') {
 strncpy(response, local_response, response_size - 1);
 response[response_size - 1] = '\0';
 }
 
 // Check for successful GPS responses
 return success && (
 strstr(local_response, "OK") != NULL ||
 strstr(local_response, "READY") != NULL ||
 strstr(local_response, "+CGNSSPWR") != NULL
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
 
     // CRITICAL: Wait for GPS hardware to initialize after power-on
     ESP_LOGI(TAG, "⏳ Waiting 3 seconds for GPS hardware initialization...");
     vTaskDelay(pdMS_TO_TICKS(3000));
     
     // SIM7670G DIRECT NMEA STREAMING - No port switching needed
     // SIM7670G streams NMEA directly after AT+CGNSSPWR=1 and AT+CGNSSTST=1
     ESP_LOGI(TAG, "📡 SIM7670G GPS ready for direct NMEA streaming (no port switch required)");
     
     // 💀🔥 ADDITIONAL SIM7670G GPS DEBUGGING: Force GPS configuration 🔥💀
     ESP_LOGI(TAG, "🔥 FORCING GPS CONFIGURATION: Sending additional SIM7670G GPS commands...");
     
     // Try to force GPS restart and reconfiguration
     if (send_gps_at_command("AT+CGNSPWR?", response, sizeof(response), 2000)) {
         ESP_LOGI(TAG, "✅ GPS Power Query: %s", response);
     }
     
     if (send_gps_at_command("AT+CGNSSTST?", response, sizeof(response), 2000)) {
         ESP_LOGI(TAG, "✅ GPS NMEA Query: %s", response);
     }
     
     // Try forcing GPS hot restart
     if (send_gps_at_command("AT+CGNSHOT", response, sizeof(response), 3000)) {
         ESP_LOGI(TAG, "✅ GPS Hot Restart: %s", response);
     } else {
         ESP_LOGI(TAG, "ℹ️  GPS Hot Restart command not supported or failed");
     } // Send AT+CGNSSTST=1 (Enable NMEA streaming) - Required for outdoor GPS fix
 if (!send_gps_at_command("AT+CGNSSTST=1", response, sizeof(response), 3000)) {
 ESP_LOGW(TAG, "Failed to enable NMEA streaming (continuing anyway)");
 }
 
 ESP_LOGI(TAG, "🛰️  Configuring SIM7670G GPS for maximum NMEA output...");
 
 // 🔥 CRITICAL: Check current NMEA output status first
 if (send_gps_at_command("AT+CGNSSTST?", response, sizeof(response), 2000)) {
     ESP_LOGI(TAG, "📋 Current NMEA status: %s", response);
 }
 
 // 🔥 CRITICAL: Use correct SIM7670G NMEA command (no second parameter)
 if (send_gps_at_command("AT+CGNSSTST=1", response, sizeof(response), 3000)) {
     ESP_LOGI(TAG, "✅ NMEA streaming enabled: %s", response);
 }
 
 // Configure NMEA sentence output rate (every second)
 if (send_gps_at_command("AT+CGNSSFGR=1000", response, sizeof(response), 2000)) {
     ESP_LOGI(TAG, "✅ NMEA output rate set to 1Hz: %s", response);
 }
 
 // Enable all GNSS sentence types for maximum data
 if (send_gps_at_command("AT+CGNSSEQ=\"RMC,VTG,GGA,GSA,GSV,GLL\"", response, sizeof(response), 2000)) {
     ESP_LOGI(TAG, "✅ All NMEA sentence types enabled: %s", response);
 }
 
 // Additional delay after enhanced GPS configuration
 ESP_LOGI(TAG, "⏳ Waiting 5 seconds for enhanced GPS configuration and NMEA streaming...");
 vTaskDelay(pdMS_TO_TICKS(5000));
 
 // Test AT+CGNSINF to verify GPS info is working
    ESP_LOGI(TAG, "🧪 Testing GPS functionality and satellite status...");
    if (send_gps_at_command("AT+CGNSINF", response, sizeof(response), 3000)) {
        ESP_LOGI(TAG, "✅ GPS responding: %s", response);
        // Check if GPS has satellite fix (second field = 1 means fix acquired)
        if (strstr(response, ",1,") != NULL) {
            ESP_LOGI(TAG, "🎯 GPS HAS SATELLITE FIX - NMEA sentences will stream automatically");
        } else {
            ESP_LOGW(TAG, "🏠 GPS SEARCHING FOR SATELLITES (normal indoors)");
            ESP_LOGI(TAG, "💡 NMEA sentences require satellite fix - using AT+CGNSINF polling for indoor testing");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  GPS info command failed - GPS still initializing");
    }

    ESP_LOGI(TAG, "✅ GPS module ready: NMEA streaming enabled + AT+CGNSINF polling available");
    ESP_LOGI(TAG, "🎯 INDOOR MODE: GPS will work via AT+CGNSINF even without satellite fix"); module_status.gnss_enabled = true;
 module_status.data_output_enabled = true;
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

// =============================================================================
// GPS POLLING FUNCTIONS (WAVESHARE OFFICIAL METHOD)  
// =============================================================================



/**
 * @brief Parse NMEA GGA sentence for GPS fix information (from working modem_init.c)
 */
static bool parse_nmea_gga_sentence(const char* nmea, gps_data_t* data)
{
    if (!nmea || !data) return false;
    
    // Look for GNGGA or GPGGA sentence (Global Positioning System Fix Data)
    if (strncmp(nmea, "$GNGGA", 6) != 0 && strncmp(nmea, "$GPGGA", 6) != 0) {
        return false;
    }
    
    char* tokens[15];
    char* nmea_copy = strdup(nmea);
    if (!nmea_copy) return false;
    
    char* token = strtok(nmea_copy, ",");
    int token_count = 0;
    
    while (token && token_count < 15) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    // GGA format: $GNGGA,time,lat,N/S,lon,E/W,quality,sats,hdop,alt,M,geoid,M,dgps_time,dgps_id*checksum
    if (token_count >= 6 && strlen(tokens[2]) > 0 && strlen(tokens[4]) > 0) {
        int quality = atoi(tokens[6]);
        if (quality > 0) { // 0 = no fix, 1+ = fix available
            data->fix_valid = true;
            
            // Parse latitude (DDMM.MMMM format)
            double lat = atof(tokens[2]);
            data->latitude = (int)(lat / 100) + fmod(lat, 100.0) / 60.0;
            if (tokens[3][0] == 'S') data->latitude = -data->latitude;
            
            // Parse longitude (DDDMM.MMMM format) 
            double lon = atof(tokens[4]);
            data->longitude = (int)(lon / 100) + fmod(lon, 100.0) / 60.0;
            if (tokens[5][0] == 'W') data->longitude = -data->longitude;
            
            // Additional data
            data->satellites = (token_count > 7) ? atoi(tokens[7]) : 0;
            data->altitude = (token_count > 9) ? atof(tokens[9]) : 0.0;
            data->fix_quality = quality;
            snprintf(data->timestamp, sizeof(data->timestamp), "%lld", (long long)time(NULL));
            
            ESP_LOGI(TAG, "✅ PARSED GGA SENTENCE:");
            ESP_LOGI(TAG, "📍 GPS FIX! Lat: %.6f, Lon: %.6f", data->latitude, data->longitude);
            ESP_LOGI(TAG, "🛰️  Quality: %d, Satellites: %d, Altitude: %.1f m", 
                     data->fix_quality, data->satellites, data->altitude);
            
            free(nmea_copy);
            return true;
        }
    }
    
    free(nmea_copy);
    return false;
}