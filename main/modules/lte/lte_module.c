#include "lte_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "string.h"
#include "stdlib.h"
#include "../../task_manager.h"

static const char *TAG = "LTE_MODULE";

// Constants
#define AT_RESPONSE_MAX_SIZE 1024

// =============================================================================
// MODULAR DEBUG LOGGING SYSTEM
// ==============================

// Debug control flags - REDUCED VERBOSITY FOR MQTT DEBUGGING
static bool debug_at_commands = true; // Log AT commands and responses
static bool debug_uart_data = false; // DISABLED - Log raw UART data (causes cache errors in timer context)
static bool debug_network_ops = true; // Log network operations
static bool debug_connection = true; // Log connection status changes
static bool debug_signal_info = false; // DISABLED - Log signal strength updates (too verbose)
static bool debug_raw_responses = false; // DISABLED - Log complete raw AT responses (too verbose)
static bool debug_timing = false; // DISABLED - Log timing information (too verbose)
static bool debug_registration = true; // Log detailed network registration process

// Debug logging macros - can be easily disabled by changing to empty macros
#define LTE_DEBUG_AT(format, ...) do { \
 if (debug_at_commands) { \
 ESP_LOGI(TAG, "[AT]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_UART(format, ...) do { \
 if (debug_uart_data) { \
 ESP_LOGI(TAG, "[UART]" format, ##__VA_ARGS__); \
 } \
} while(0)

// Unused function removed - continuous_uart_monitor was not being used

#define LTE_DEBUG_NET(format, ...) do { \
 if (debug_network_ops) { \
 ESP_LOGI(TAG, "[NET]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_CONN(format, ...) do { \
 if (debug_connection) { \
 ESP_LOGI(TAG, "[CONN]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_SIGNAL(format, ...) do { \
 if (debug_signal_info) { \
 ESP_LOGI(TAG, "[SIGNAL]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_RAW(format, ...) do { \
 if (debug_raw_responses) { \
 ESP_LOGI(TAG, "[RAW]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_TIMING(format, ...) do { \
 if (debug_timing) { \
 ESP_LOGI(TAG, "[TIMING]" format, ##__VA_ARGS__); \
 } \
} while(0)

#define LTE_DEBUG_REG(format, ...) do { \
 if (debug_registration) { \
 ESP_LOGI(TAG, "[REG]" format, ##__VA_ARGS__); \
 } \
} while(0)

// Hardware debugging - UART pin configuration
#define LTE_DEBUG_PINS(tx, rx) do { \
 ESP_LOGI(TAG, "[PINS] UART TX=%d, RX=%d (ESP32 -> Modem TX=%d, ESP32 <- Modem RX=%d)", \
 tx, rx, tx, rx); \
} while(0)

// Module state
static lte_config_t current_config = {0};
static lte_module_status_t module_status = {0};
static bool module_initialized = false;

// NMEA preservation buffer (shared across AT commands)
static char preserved_nmea_buffer[2048] = {0};
static size_t preserved_nmea_length = 0;

// Private function prototypes
static bool lte_init_impl(const lte_config_t* config);
static bool lte_deinit_impl(void);
static bool lte_connect_impl(void);
static bool lte_disconnect_impl(void);
static lte_status_t lte_get_connection_status_impl(void);
static bool lte_get_status_impl(lte_module_status_t* status);
static bool lte_get_network_info_impl(lte_network_info_t* info);
static bool lte_send_at_command_impl(const char* command, at_response_t* response, int timeout_ms);
static bool lte_read_raw_data_impl(char* buffer, size_t buffer_size, size_t* bytes_read, int timeout_ms);
static bool lte_get_preserved_nmea_impl(char* buffer, size_t buffer_size, size_t* data_length);
static bool lte_set_apn_impl(const char* apn, const char* username, const char* password);
static bool lte_check_sim_ready_impl(void);
static bool lte_get_signal_strength_impl(int* rssi, int* quality);
static void lte_set_debug_impl(bool enable);

// Helper functions
static bool wait_for_at_response(const char* expected, at_response_t* response, int timeout_ms);
static bool parse_signal_quality(const char* response, int* rssi, int* quality);
static bool parse_network_info(const char* response, lte_network_info_t* info);

// LTE interface implementation
static const lte_interface_t lte_interface = {
 .init = lte_init_impl,
 .deinit = lte_deinit_impl,
 .connect = lte_connect_impl,
 .disconnect = lte_disconnect_impl,
 .get_connection_status = lte_get_connection_status_impl,
 .get_status = lte_get_status_impl,
 .get_network_info = lte_get_network_info_impl,
 .send_at_command = lte_send_at_command_impl,
 .read_raw_data = lte_read_raw_data_impl,
 .get_preserved_nmea = lte_get_preserved_nmea_impl,
 .set_apn = lte_set_apn_impl,
 .check_sim_ready = lte_check_sim_ready_impl,
 .get_signal_strength = lte_get_signal_strength_impl,
 .set_debug = lte_set_debug_impl
};

const lte_interface_t* lte_get_interface(void)
{
 return &lte_interface;
}

static bool lte_init_impl(const lte_config_t* config)
{
 if (!config) {
 ESP_LOGE(TAG, "Configuration is NULL");
 return false;
 }
 
 if (module_initialized) {
 ESP_LOGW(TAG, "LTE module already initialized");
 return true;
 }
 
 // Store configuration
 memcpy(&current_config, config, sizeof(lte_config_t));
 
 // Update debug flags from config
 debug_at_commands = config->debug_at_commands;
 debug_network_ops = config->debug_output;
 debug_connection = config->debug_output;
 
 LTE_DEBUG_NET("Initializing LTE module with APN: '%s'", config->apn);
 LTE_DEBUG_NET("Network timeout: %d ms, Max retries: %d", 
 config->network_timeout_ms, config->max_retries);
 
 // Initialize UART for SIM7670G communication
 // Following Waveshare ESP32-S3-SIM7670G documentation
 uart_config_t uart_config = {
 .baud_rate = 115200, // Standard baud rate for SIM7670G
 .data_bits = UART_DATA_8_BITS,
 .parity = UART_PARITY_DISABLE,
 .stop_bits = UART_STOP_BITS_1,
 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
 .source_clk = UART_SCLK_APB,
 };
 
 // Configure UART parameters
 esp_err_t ret = uart_param_config(UART_NUM_1, &uart_config);
 if (ret != ESP_OK) {
 ESP_LOGE(TAG, "Failed to configure UART parameters: %s", esp_err_to_name(ret));
 return false;
 }
 
 // Set UART pins (TX=18, RX=17 for Waveshare ESP32-S3-SIM7670G) - PINS FLIPPED!
 ret = uart_set_pin(UART_NUM_1, 18, 17, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
 if (ret != ESP_OK) {
 ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
 return false;
 }
 
 // Install UART driver with 4KB buffer (matching GPS buffer requirement)
 ret = uart_driver_install(UART_NUM_1, 4096, 4096, 0, NULL, 0);
 if (ret != ESP_OK) {
 ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
 return false;
 }
 
 LTE_DEBUG_PINS(18, 17); // Log successful UART initialization (TX=18, RX=17 - CORRECTED!)
 ESP_LOGI(TAG, "UART driver initialized successfully for SIM7670G (TX=18, RX=17)");
 
 // Initialize module status
 memset(&module_status, 0, sizeof(module_status));
 module_status.connection_status = LTE_STATUS_DISCONNECTED;
 
 // Wait for SIM7670G module to be ready (Waveshare recommends delay after UART init)
 // Reset watchdog before long delay to prevent timeout
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(3000));
 
 // ===================================================================
 // CRITICAL: GPS Port Switching FIRST (before any AT commands!)
 // The SIM7670G boots with GPS enabled by default, interfering with AT
 // ===================================================================
 ESP_LOGI(TAG, " Disabling GPS interference BEFORE AT communication...");
 
 // Send raw AT commands to disable GPS without error checking (GPS might be active)
 uart_flush_input(UART_NUM_1);
 uart_write_bytes(UART_NUM_1, "AT+CGNSSPWR=0\r\n", 15);
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(2000));
 uart_flush_input(UART_NUM_1);
 
 uart_write_bytes(UART_NUM_1, "AT+CGNSSTST=0\r\n", 15); 
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(1000));
 uart_flush_input(UART_NUM_1);
 
 // Removed AT+CGNSSPORTSWITCH - not documented in Waveshare official reference
 
 ESP_LOGI(TAG, " GPS interference disabled - AT commands should work now");
 
 // Test AT communication
 at_response_t response;
 for (int i = 0; i < current_config.max_retries; i++) {
 if (lte_send_at_command_impl("AT", &response, 2000)) {
 break;
 }
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(1000));
 if (i == current_config.max_retries - 1) {
 ESP_LOGE(TAG, "Failed to establish AT communication");
 return false;
 }
 }
 
 // Set full functionality (reduced timeout to prevent watchdog)
 if (!lte_send_at_command_impl("AT+CFUN=1", &response, 5000)) {
 ESP_LOGE(TAG, "Failed to set full functionality");
 return false;
 }
 
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(2000));
 
 // Check SIM status (GPS interference should be disabled by now)
 if (!lte_check_sim_ready_impl()) {
 ESP_LOGE(TAG, "SIM card not ready");
 return false;
 }
 
 module_status.initialized = true;
 module_initialized = true;
 
 if (config->debug_output) {
 ESP_LOGI(TAG, "LTE module initialized successfully");
 ESP_LOGI(TAG, " APN: '%s'", config->apn);
 ESP_LOGI(TAG, " Network timeout: %d ms", config->network_timeout_ms);
 ESP_LOGI(TAG, " Max retries: %d", config->max_retries);
 }
 
 return true;
}

static bool lte_deinit_impl(void)
{
 if (!module_initialized) {
 return true;
 }
 
 // Disconnect if connected
 lte_disconnect_impl();
 
 // Clean up UART driver
 esp_err_t ret = uart_driver_delete(UART_NUM_1);
 if (ret != ESP_OK) {
 ESP_LOGW(TAG, "Failed to delete UART driver: %s", esp_err_to_name(ret));
 }
 
 // Reset module status
 memset(&module_status, 0, sizeof(module_status));
 module_initialized = false;
 
 ESP_LOGI(TAG, "LTE module deinitialized with UART cleanup");
 return true;
}

static bool lte_connect_impl(void)
{
 if (!module_initialized) {
 ESP_LOGE(TAG, "LTE module not initialized");
 return false;
 }
 
 if (module_status.connection_status == LTE_STATUS_CONNECTED) {
 ESP_LOGI(TAG, "Already connected to network");
 return true;
 }
 
 LTE_DEBUG_NET("=== STARTING CELLULAR NETWORK CONNECTION ===");
 LTE_DEBUG_NET("APN: '%s'", current_config.apn);
 LTE_DEBUG_NET("Network timeout: %d ms", current_config.network_timeout_ms);
 ESP_LOGI(TAG, "Connecting to cellular network...");
 module_status.connection_status = LTE_STATUS_CONNECTING;
 
 at_response_t response;
 
 // Set APN with enhanced debug
 LTE_DEBUG_NET("Step 1: Setting APN configuration...");
 if (!lte_set_apn_impl(current_config.apn, current_config.username, current_config.password)) {
 LTE_DEBUG_NET("FAILED: APN configuration failed");
 module_status.connection_status = LTE_STATUS_ERROR;
 return false;
 }
 LTE_DEBUG_NET("SUCCESS: APN configured");
 
 // Enhanced network registration with maximum debug
 LTE_DEBUG_REG("Step 2: Starting network registration process...");
 uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
 int registration_attempts = 0;
 
 while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < current_config.network_timeout_ms) {
 registration_attempts++;
 uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - start_time;
 
 LTE_DEBUG_REG("Registration attempt #%d (elapsed: %d ms)", registration_attempts, elapsed);
 
 // Check multiple registration commands for maximum visibility
 if (lte_send_at_command_impl("AT+CREG?", &response, 2000)) {
 LTE_DEBUG_REG("CREG response: '%s'", response.response);
 
 if (strstr(response.response, "+CREG: 0,1")) {
 LTE_DEBUG_REG("SUCCESS: Registered on home network");
 break;
 } else if (strstr(response.response, "+CREG: 0,5")) {
 LTE_DEBUG_REG("SUCCESS: Registered roaming"); 
 break;
 } else if (strstr(response.response, "+CREG: 0,2")) {
 LTE_DEBUG_REG("STATUS: Searching for network...");
 } else if (strstr(response.response, "+CREG: 0,0")) {
 LTE_DEBUG_REG("STATUS: Not registered, not searching");
 } else if (strstr(response.response, "+CREG: 0,3")) {
 LTE_DEBUG_REG("ERROR: Registration denied");
 } else {
 LTE_DEBUG_REG("UNKNOWN: Unexpected CREG response");
 }
 } else {
 LTE_DEBUG_REG("ERROR: CREG command failed");
 }
 
 // Also check signal strength during registration
 if (lte_send_at_command_impl("AT+CSQ", &response, 2000)) {
 LTE_DEBUG_SIGNAL("Signal during registration: %s", response.response);
 }
 
 esp_task_wdt_reset();
 vTaskDelay(pdMS_TO_TICKS(1000));
 }
 
 // Check final registration status
 uint32_t final_elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - start_time;
 LTE_DEBUG_REG("Registration process completed in %d ms (%d attempts)", final_elapsed, registration_attempts);
 
 // Activate PDP context (reduced timeout to prevent watchdog)
 if (!lte_send_at_command_impl("AT+CGACT=1,1", &response, 8000)) {
 ESP_LOGW(TAG, "Failed to activate PDP context, trying alternative");
 if (!lte_send_at_command_impl("AT+CGATT=1", &response, 8000)) {
 ESP_LOGE(TAG, "Failed to attach to network");
 module_status.connection_status = LTE_STATUS_ERROR;
 return false;
 }
 }
 
 module_status.pdp_active = true;
 module_status.connection_status = LTE_STATUS_CONNECTED;
 module_status.connection_uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
 
 // Get network info
 lte_get_network_info_impl(&module_status.network_info);
 
 ESP_LOGI(TAG, "Connected to cellular network");
 return true;
}

static bool lte_disconnect_impl(void)
{
 if (!module_initialized) {
 return true;
 }
 
 at_response_t response;
 
 // Deactivate PDP context
 lte_send_at_command_impl("AT+CGACT=0,1", &response, 10000);
 
 module_status.connection_status = LTE_STATUS_DISCONNECTED;
 module_status.pdp_active = false;
 
 ESP_LOGI(TAG, "Disconnected from cellular network");
 return true;
}

static lte_status_t lte_get_connection_status_impl(void)
{
 return module_status.connection_status;
}

static bool lte_get_status_impl(lte_module_status_t* status)
{
 if (!status) {
 return false;
 }
 
 memcpy(status, &module_status, sizeof(lte_module_status_t));
 return true;
}

static bool lte_get_network_info_impl(lte_network_info_t* info)
{
 if (!info || !module_initialized) {
 return false;
 }
 
 at_response_t response;
 
 // Get operator name
 if (lte_send_at_command_impl("AT+COPS?", &response, 5000)) {
 parse_network_info(response.response, info);
 }
 
 // Get signal strength
 int rssi, quality;
 if (lte_get_signal_strength_impl(&rssi, &quality)) {
 info->signal_strength = rssi;
 info->signal_quality = quality;
 }
 
 return true;
}

static bool lte_send_at_command_impl(const char* command, at_response_t* response, int timeout_ms)
{
 if (!command || !response) {
 LTE_DEBUG_AT("ERROR: Invalid parameters - command=%p, response=%p", command, response);
 return false;
 }

 // Reset watchdog before potentially long AT command operations
 esp_task_wdt_reset();

 // Clear response
 memset(response, 0, sizeof(at_response_t));
 
 // Send AT command directly via UART
 char local_response[256] = {0};
 
 // Send command
 uart_write_bytes(UART_NUM_1, command, strlen(command));
 uart_write_bytes(UART_NUM_1, "\r\n", 2);
 
 // Read response
 int bytes_read = uart_read_bytes(UART_NUM_1, local_response, sizeof(local_response) - 1, pdMS_TO_TICKS(timeout_ms));
 
 // Copy response to at_response_t structure
 if (bytes_read > 0) {
     local_response[bytes_read] = '\0';
     strncpy(response->response, local_response, 511);
     response->response[511] = '\0';
     response->success = true;
     response->response_time_ms = timeout_ms; // Placeholder
 } else {
     response->success = false;
     response->response[0] = '\0';
     response->response_time_ms = timeout_ms;
 }

 LTE_DEBUG_AT("LTE AT CMD: %s", command);
 
 LTE_DEBUG_AT("LTE AT RESP: %s (success: %s)", 
              response->response, response->success ? "YES" : "NO");
              
 return response->success;
 
}

static bool lte_read_raw_data_impl(char* buffer, size_t buffer_size, size_t* bytes_read, int timeout_ms)
{
 if (!buffer || buffer_size == 0 || !bytes_read) {
 ESP_LOGE(TAG, " Invalid parameters for raw data read");
 return false;
 }
 
 *bytes_read = 0;
 
 LTE_DEBUG_UART(" === ENHANCED RAW UART READ START ===");
 LTE_DEBUG_UART(" Reading raw UART data (timeout: %d ms, buffer: %d bytes)", timeout_ms, buffer_size);
 
 // ENHANCED DEBUG: Check UART status first
 size_t available_before = 0;
 uart_get_buffered_data_len(UART_NUM_1, &available_before);
 LTE_DEBUG_UART(" UART buffer status at start: %d bytes available", available_before);
 
 // Read raw data from UART (for NMEA sentences after GPS enable)
 // SIM7670G outputs NMEA data directly to UART after AT+CGNSSTST=1
 
 TickType_t start_time = xTaskGetTickCount();
 size_t total_read = 0;
 
 while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms) && total_read < (buffer_size - 1)) {
 size_t available_bytes = 0;
 esp_err_t err = uart_get_buffered_data_len(UART_NUM_1, &available_bytes);
 
 if (err == ESP_OK && available_bytes > 0) {
 size_t to_read = (available_bytes < (buffer_size - 1 - total_read)) ? 
 available_bytes : (buffer_size - 1 - total_read);
 
 int len = uart_read_bytes(UART_NUM_1, buffer + total_read, to_read, pdMS_TO_TICKS(100));
 
 if (len > 0) {
 total_read += len;
 LTE_DEBUG_UART(" Read %d bytes (total: %d)", len, total_read);
 
 // Check if we have complete NMEA sentences (ending with \n)
 buffer[total_read] = '\0';
 if (strchr(buffer, '\n')) {
 LTE_DEBUG_UART(" Found complete NMEA sentences");
 break;
 }
 }
 } else {
 // No data available, small delay
 vTaskDelay(pdMS_TO_TICKS(50));
 }
 }
 
 buffer[total_read] = '\0'; // Ensure null termination
 *bytes_read = total_read;
 
 if (total_read > 0) {
 LTE_DEBUG_UART(" Raw UART data (%d bytes):\n%.*s", total_read, (int)total_read, buffer);
 
 // ENHANCED DEBUG: Show hex dump of ALL UART data
 LTE_DEBUG_UART(" Raw UART hex dump:");
 for (size_t i = 0; i < total_read; i += 16) {
 char hex_line[64] = {0};
 char ascii_line[17] = {0};
 
 for (size_t j = 0; j < 16 && (i + j) < total_read; j++) {
 sprintf(hex_line + (j * 3), "%02X ", (unsigned char)buffer[i + j]);
 ascii_line[j] = (buffer[i + j] >= 32 && buffer[i + j] <= 126) ? buffer[i + j] : '.';
 }
 
 LTE_DEBUG_UART(" %04X: %-48s |%s|", i, hex_line, ascii_line);
 }
 
 // ENHANCED DEBUG: Check for specific patterns
 if (strstr(buffer, "$G")) {
 LTE_DEBUG_UART(" NMEA GPS sentences detected!");
 }
 if (strstr(buffer, "+C")) {
 LTE_DEBUG_UART(" AT command responses detected!");
 }
 if (strstr(buffer, "OK")) {
 LTE_DEBUG_UART(" AT OK responses detected!");
 }
 if (strstr(buffer, "ERROR")) {
 LTE_DEBUG_UART(" AT ERROR responses detected!");
 }
 
 return true;
 } else {
 LTE_DEBUG_UART(" No raw UART data available");
 return false;
 }
}

static bool lte_get_preserved_nmea_impl(char* buffer, size_t buffer_size, size_t* data_length)
{
 if (!buffer || !data_length || buffer_size == 0) {
 return false;
 }

 if (preserved_nmea_length > 0 && preserved_nmea_length < buffer_size) {
 memcpy(buffer, preserved_nmea_buffer, preserved_nmea_length);
 buffer[preserved_nmea_length] = '\0';
 *data_length = preserved_nmea_length;
 
 LTE_DEBUG_UART(" Retrieved %d bytes of preserved NMEA data", preserved_nmea_length);
 
 // Clear the buffer after retrieval
 preserved_nmea_length = 0;
 memset(preserved_nmea_buffer, 0, sizeof(preserved_nmea_buffer));
 
 return true;
 }

 *data_length = 0;
 return false;
}

static bool lte_set_apn_impl(const char* apn, const char* username, const char* password)
{
 if (!apn) {
 return false;
 }
 
 char apn_cmd[128];
 snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
 
 at_response_t response;
 bool success = lte_send_at_command_impl(apn_cmd, &response, 5000);
 
 if (success && current_config.debug_output) {
 ESP_LOGI(TAG, "APN set to '%s'", apn);
 }
 
 return success;
}

static bool lte_check_sim_ready_impl(void)
{
 at_response_t response;
 
 if (lte_send_at_command_impl("AT+CPIN?", &response, 5000)) {
 if (strstr(response.response, "READY")) {
 module_status.sim_ready = true;
 ESP_LOGI(TAG, "SIM card is ready");
 return true;
 }
 }
 
 module_status.sim_ready = false;
 ESP_LOGE(TAG, "SIM card not ready");
 return false;
}

static bool lte_get_signal_strength_impl(int* rssi, int* quality)
{
 if (!rssi || !quality) {
 return false;
 }
 
 at_response_t response;
 if (lte_send_at_command_impl("AT+CSQ", &response, 2000)) {
 return parse_signal_quality(response.response, rssi, quality);
 }
 
 return false;
}

static void lte_set_debug_impl(bool enable)
{
 current_config.debug_output = enable;
 current_config.debug_at_commands = enable;
 
 // Update our modular debug flags
 debug_at_commands = enable;
 debug_network_ops = enable; 
 debug_connection = enable;
 
 ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// =============================================================================
// MODULAR DEBUG CONTROL FUNCTIONS
// =============================================================================

void lte_set_debug_at_commands(bool enable) 
{
 debug_at_commands = enable;
 LTE_DEBUG_AT("AT command debug %s", enable ? "enabled" : "disabled");
}

void lte_set_debug_uart_data(bool enable) 
{
 debug_uart_data = enable;
 LTE_DEBUG_UART("UART data debug %s", enable ? "enabled" : "disabled");
}

void lte_set_debug_network(bool enable) 
{
 debug_network_ops = enable;
 LTE_DEBUG_NET("Network debug %s", enable ? "enabled" : "disabled");
}

void lte_set_debug_connection(bool enable) 
{
 debug_connection = enable;
 LTE_DEBUG_CONN("Connection debug %s", enable ? "enabled" : "disabled");
}

void lte_set_debug_signal(bool enable) 
{
 debug_signal_info = enable;
 LTE_DEBUG_SIGNAL("Signal debug %s", enable ? "enabled" : "disabled");
}

// Enable all debug modes - MAXIMUM CELLULAR DEBUGGING 
void lte_enable_all_debug(void) 
{
 debug_at_commands = true;
 debug_uart_data = true; 
 debug_network_ops = true;
 debug_connection = true;
 debug_signal_info = true;
 debug_raw_responses = true;
 debug_timing = true;
 debug_registration = true;
 ESP_LOGI(TAG, " MAXIMUM CELLULAR DEBUG MODE ACTIVATED");
 ESP_LOGI(TAG, "All debug categories enabled for troubleshooting");
}

// Enable maximum debug for interactive troubleshooting
void lte_enable_interactive_debug(void) 
{
 lte_enable_all_debug();
 ESP_LOGI(TAG, "=== INTERACTIVE DEBUG SESSION STARTED ===");
 ESP_LOGI(TAG, " Ready for cellular troubleshooting");
 ESP_LOGI(TAG, " Monitor serial output for detailed logs");
 ESP_LOGI(TAG, " If stuck, check UART pins or try pin swap");
}

// Show current debug status 
void lte_show_debug_status(void) 
{
 ESP_LOGI(TAG, "=== CELLULAR DEBUG STATUS ===");
 ESP_LOGI(TAG, "AT Commands: %s", debug_at_commands ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "UART Data: %s", debug_uart_data ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Network Ops: %s", debug_network_ops ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Connection: %s", debug_connection ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Signal Info: %s", debug_signal_info ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Raw Responses: %s", debug_raw_responses ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Timing: %s", debug_timing ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "Registration: %s", debug_registration ? "ENABLED" : "disabled");
 ESP_LOGI(TAG, "===============================");
}

// Disable all debug modes - for production
void lte_disable_all_debug(void) 
{
 debug_at_commands = false;
 debug_uart_data = false;
 debug_network_ops = false; 
 debug_connection = false;
 debug_signal_info = false;
 ESP_LOGI(TAG, "All debug modes disabled");
}

// =============================================================================
// UART PIN CONFIGURATION HELPERS 
// =============================================================================

void lte_log_uart_config(int tx_pin, int rx_pin) 
{
 ESP_LOGI(TAG, "=== UART CONFIGURATION ===");
 ESP_LOGI(TAG, "ESP32-S3 TX Pin: %d -> SIM7670G RX", tx_pin);
 ESP_LOGI(TAG, "ESP32-S3 RX Pin: %d <- SIM7670G TX", rx_pin); 
 ESP_LOGI(TAG, "Baud Rate: 115200");
 ESP_LOGI(TAG, "==========================");
 ESP_LOGI(TAG, "If AT commands fail, try swapping TX/RX pins:");
 ESP_LOGI(TAG, " Current: TX=%d, RX=%d", tx_pin, rx_pin);
 ESP_LOGI(TAG, " Try: TX=%d, RX=%d", rx_pin, tx_pin);
}

// Helper function implementations
static bool wait_for_at_response(const char* expected, at_response_t* response, int timeout_ms)
{
 if (!expected || !response) {
 return false;
 }
 
 char* buffer = malloc(512);
 if (!buffer) {
 return false;
 }
 
 int total_len = 0;
 TickType_t start_time = xTaskGetTickCount();
 
 while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
 
 int len = uart_read_bytes(UART_NUM_1, buffer + total_len, 
 511 - total_len, pdMS_TO_TICKS(100));
 if (len > 0) {
 total_len += len;
 buffer[total_len] = '\0';
 
 // Copy to response buffer
 strncpy(response->response, buffer, sizeof(response->response) - 1);
 response->response[sizeof(response->response) - 1] = '\0';
 
 if (strstr(buffer, expected)) {
 free(buffer);
 return true;
 }
 
 // Check for error responses
 if (strstr(buffer, "ERROR") || strstr(buffer, "+CME ERROR") || strstr(buffer, "+CMS ERROR")) {
 free(buffer);
 return false;
 }
 }
 }
 
 free(buffer);
 return false;
}

static bool parse_signal_quality(const char* response, int* rssi, int* quality)
{
 if (!response || !rssi || !quality) {
 return false;
 }
 
 // Look for +CSQ: response
 const char* csq_pos = strstr(response, "+CSQ:");
 if (!csq_pos) {
 return false;
 }
 
 int parsed_rssi, parsed_quality;
 if (sscanf(csq_pos, "+CSQ: %d,%d", &parsed_rssi, &parsed_quality) == 2) {
 *rssi = (parsed_rssi == 99) ? -113 : (-113 + parsed_rssi * 2); // Convert to dBm
 *quality = parsed_quality;
 return true;
 }
 
 return false;
}

static bool parse_network_info(const char* response, lte_network_info_t* info)
{
 if (!response || !info) {
 return false;
 }
 
 // Parse operator name from +COPS response
 const char* cops_pos = strstr(response, "+COPS:");
 if (cops_pos) {
 // Simple parsing - look for quoted operator name
 const char* quote_start = strchr(cops_pos, '"');
 if (quote_start) {
 quote_start++; // Move past the quote
 const char* quote_end = strchr(quote_start, '"');
 if (quote_end && (quote_end - quote_start) < sizeof(info->operator_name) - 1) {
 strncpy(info->operator_name, quote_start, quote_end - quote_start);
 info->operator_name[quote_end - quote_start] = '\0';
 }
 }
 }
 
 return true;
}

// Utility functions
const char* lte_status_to_string(lte_status_t status)
{
 switch (status) {
 case LTE_STATUS_DISCONNECTED: return "Disconnected";
 case LTE_STATUS_CONNECTING: return "Connecting";
 case LTE_STATUS_CONNECTED: return "Connected";
 case LTE_STATUS_ERROR: return "Error";
 default: return "Unknown";
 }
}

bool lte_is_connected(void)
{
 return module_status.connection_status == LTE_STATUS_CONNECTED;
}

bool lte_format_network_info(const lte_network_info_t* info, char* buffer, size_t buffer_size)
{
 if (!info || !buffer || buffer_size < 64) {
 return false;
 }
 
 snprintf(buffer, buffer_size, "Operator: %s, Signal: %d dBm, Quality: %d",
 info->operator_name, info->signal_strength, info->signal_quality);
 return true;
}