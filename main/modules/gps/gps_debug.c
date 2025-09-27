#include "gps_debug.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "GPS_DEBUG";

// Debug module state
static gps_debug_config_t debug_config = {0};
static bool debug_initialized = false;

// Debug implementation functions
static bool gps_debug_init_impl(const gps_debug_config_t* config);
static void gps_debug_deinit_impl(void);
static void gps_debug_log_uart_read_attempt_impl(int attempt, int total_attempts);
static void gps_debug_log_uart_read_result_impl(bool success, size_t bytes_read);
static void gps_debug_log_uart_data_impl(const char* buffer, size_t size);
static void gps_debug_log_hex_dump_impl(const char* buffer, size_t size);
static void gps_debug_log_nmea_analysis_impl(const char* buffer, size_t size);
static void gps_debug_log_at_command_impl(const char* command, const char* response);
static void gps_debug_set_verbose_level_impl(int level);

// Debug interface
static const gps_debug_interface_t gps_debug_interface = {
 .init = gps_debug_init_impl,
 .deinit = gps_debug_deinit_impl,
 .log_uart_read_attempt = gps_debug_log_uart_read_attempt_impl,
 .log_uart_read_result = gps_debug_log_uart_read_result_impl,
 .log_uart_data = gps_debug_log_uart_data_impl,
 .log_hex_dump = gps_debug_log_hex_dump_impl,
 .log_nmea_analysis = gps_debug_log_nmea_analysis_impl,
 .log_at_command = gps_debug_log_at_command_impl,
 .set_verbose_level = gps_debug_set_verbose_level_impl
};

const gps_debug_interface_t* gps_debug_get_interface(void)
{
 return &gps_debug_interface;
}

static bool gps_debug_init_impl(const gps_debug_config_t* config)
{
 if (!config) {
 ESP_LOGE(TAG, "Debug config is NULL");
 return false;
 }
 
 memcpy(&debug_config, config, sizeof(gps_debug_config_t));
 debug_initialized = true;
 
 ESP_LOGI(TAG, " GPS Debug Module Initialized");
 ESP_LOGI(TAG, " Verbose UART: %s", debug_config.enable_verbose_uart ? "ENABLED" : "DISABLED");
 ESP_LOGI(TAG, " Hex dumps: %s", debug_config.enable_hex_dumps ? "ENABLED" : "DISABLED");
 ESP_LOGI(TAG, " NMEA analysis: %s", debug_config.enable_nmea_analysis ? "ENABLED" : "DISABLED");
 ESP_LOGI(TAG, " Timing logs: %s", debug_config.enable_timing_logs ? "ENABLED" : "DISABLED");
 ESP_LOGI(TAG, " Command tracking: %s", debug_config.enable_command_tracking ? "ENABLED" : "DISABLED");
 
 return true;
}

static void gps_debug_deinit_impl(void)
{
 if (debug_initialized) {
 ESP_LOGI(TAG, " GPS Debug Module Deinitialized");
 memset(&debug_config, 0, sizeof(gps_debug_config_t));
 debug_initialized = false;
 }
}

static void gps_debug_log_uart_read_attempt_impl(int attempt, int total_attempts)
{
 if (!debug_initialized || !debug_config.enable_verbose_uart) {
 return;
 }
 
 ESP_LOGI(TAG, " === UART READ ATTEMPT %d/%d ===", attempt, total_attempts);
}

static void gps_debug_log_uart_read_result_impl(bool success, size_t bytes_read)
{
 if (!debug_initialized || !debug_config.enable_verbose_uart) {
 return;
 }
 
 ESP_LOGI(TAG, " Read result: success=%s, bytes_read=%d", 
 success ? "TRUE" : "FALSE", (int)bytes_read);
}

static void gps_debug_log_uart_data_impl(const char* buffer, size_t size)
{
 if (!debug_initialized || !debug_config.enable_verbose_uart || !buffer) {
 return;
 }
 
 ESP_LOGI(TAG, " Raw UART data (%d bytes):", (int)size);
 ESP_LOGI(TAG, "%.*s", (int)size, buffer);
}

static void gps_debug_log_hex_dump_impl(const char* buffer, size_t size)
{
 if (!debug_initialized || !debug_config.enable_hex_dumps || !buffer) {
 return;
 }
 
 ESP_LOGI(TAG, " Hex dump of first 64 bytes:");
 for (int i = 0; i < size && i < 64; i += 16) {
 char hex_line[80] = {0};
 char *hex_ptr = hex_line;
 for (int j = 0; j < 16 && (i + j) < size && (i + j) < 64; j++) {
 hex_ptr += sprintf(hex_ptr, "%02X ", (unsigned char)buffer[i + j]);
 }
 ESP_LOGI(TAG, " %04X: %s", i, hex_line);
 }
}

static void gps_debug_log_nmea_analysis_impl(const char* buffer, size_t size)
{
 if (!debug_initialized || !debug_config.enable_nmea_analysis || !buffer) {
 return;
 }
 
 // Look for NMEA sentences starting with $
 char *nmea_start = strchr(buffer, '$');
 if (nmea_start != NULL) {
 ESP_LOGI(TAG, " FOUND NMEA DATA! First '$' at position %d", 
 (int)(nmea_start - buffer));
 ESP_LOGI(TAG, " NMEA data snippet: %.80s", nmea_start);
 } else {
 ESP_LOGI(TAG, " No '$' character found - no NMEA data in this read");
 
 // Check for common AT responses
 if (strstr(buffer, "+CGNSSTST") != NULL) {
 ESP_LOGI(TAG, " Found CGNSSTST response in data");
 }
 if (strstr(buffer, "+CPING") != NULL) {
 ESP_LOGI(TAG, " Found CPING response in data");
 }
 if (strstr(buffer, "OK") != NULL) {
 ESP_LOGI(TAG, " Found OK response in data");
 }
 }
}

static void gps_debug_log_at_command_impl(const char* command, const char* response)
{
 if (!debug_initialized || !debug_config.enable_command_tracking) {
 return;
 }
 
 ESP_LOGI(TAG, " AT Command: %s", command ? command : "NULL");
 ESP_LOGI(TAG, " Response: %s", response ? response : "NULL");
}

static void gps_debug_set_verbose_level_impl(int level)
{
 if (!debug_initialized) {
 return;
 }
 
 // Set debug levels based on verbosity
 switch (level) {
 case 0: // Off
 debug_config.enable_verbose_uart = false;
 debug_config.enable_hex_dumps = false;
 debug_config.enable_nmea_analysis = false;
 debug_config.enable_timing_logs = false;
 debug_config.enable_command_tracking = false;
 break;
 case 1: // Basic
 debug_config.enable_verbose_uart = true;
 debug_config.enable_nmea_analysis = true;
 debug_config.enable_command_tracking = true;
 break;
 case 2: // Full
 debug_config.enable_verbose_uart = true;
 debug_config.enable_hex_dumps = true;
 debug_config.enable_nmea_analysis = true;
 debug_config.enable_timing_logs = true;
 debug_config.enable_command_tracking = true;
 break;
 }
 
 ESP_LOGI(TAG, " Debug verbosity level set to %d", level);
}