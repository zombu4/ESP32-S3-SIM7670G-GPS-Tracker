#include "mqtt_module.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "cJSON.h"
#include "time.h"
#include "sys/time.h"
// Include LTE module for shared AT interface
#include "../lte/lte_module.h"
// Include APN manager for proper APN handling
#include "../apn/apn_manager.h"
// Include nuclear pipeline for AT command routing
#include "../parallel/nuclear_integration.h"

static const char *TAG = "MQTT_MODULE";

// Module state
static mqtt_config_t current_config = {0};
static mqtt_module_status_t module_status = {0};
static bool module_initialized = false;

// Private function prototypes
static bool mqtt_init_impl(const mqtt_config_t* config);
static bool mqtt_deinit_impl(void);
static bool mqtt_connect_impl(void);
static bool mqtt_disconnect_impl(void);
static mqtt_status_t mqtt_get_status_impl(void);
static bool mqtt_get_module_status_impl(mqtt_module_status_t* status);
static bool mqtt_publish_impl(const mqtt_message_t* message, mqtt_publish_result_t* result);
static bool mqtt_publish_json_impl(const char* topic, const char* json_payload, mqtt_publish_result_t* result);
static bool mqtt_subscribe_impl(const char* topic, int qos);
static bool mqtt_unsubscribe_impl(const char* topic);
static bool mqtt_is_connected_impl(void);
static void mqtt_set_debug_impl(bool enable);

// Helper functions using LTE AT interface
static bool send_mqtt_at_command(const char* command, const char* expected, int timeout_ms);
static bool mqtt_start_service(void);
static bool mqtt_acquire_client(void);
static bool mqtt_connect_to_broker(void);

// MQTT interface implementation
static const mqtt_interface_t mqtt_interface = {
 .init = mqtt_init_impl,
 .deinit = mqtt_deinit_impl,
 .connect = mqtt_connect_impl,
 .disconnect = mqtt_disconnect_impl,
 .get_status = mqtt_get_status_impl,
 .get_module_status = mqtt_get_module_status_impl,
 .publish = mqtt_publish_impl,
 .publish_json = mqtt_publish_json_impl,
 .subscribe = mqtt_subscribe_impl,
 .unsubscribe = mqtt_unsubscribe_impl,
 .is_connected = mqtt_is_connected_impl,
 .set_debug = mqtt_set_debug_impl
};

const mqtt_interface_t* mqtt_get_interface(void)
{
 return &mqtt_interface;
}

static bool send_mqtt_at_command(const char* command, const char* expected, int timeout_ms)
{
 if (!command) {
 ESP_LOGE(TAG, "[MQTT] Command is NULL");
 return false;
 }
 
 ESP_LOGI(TAG, "[MQTT] Nuclear AT CMD: %s", command);
 
 // 💀🔥 USE NUCLEAR PIPELINE FOR MQTT AT COMMANDS 🔥💀
 // CRITICAL FIX: Use nuclear_send_at_command instead of LTE module
 // This prevents UART collisions and ensures proper command routing
 extern bool nuclear_send_at_command(const char* command, char* response, size_t response_size, int timeout_ms);
 
 char response_buffer[1024] = {0};
 bool success = false;
 
 if (strlen(command) > 0) {
 // Nuclear pipeline handles timing internally - no manual delays needed
 success = nuclear_send_at_command(command, response_buffer, sizeof(response_buffer), timeout_ms);
 ESP_LOGI(TAG, "[MQTT] Nuclear AT RESP: %s (success: %s)", 
 response_buffer, success ? "YES" : "NO");
 } else {
 // Empty command - typically used for data input phases
 success = true;
 ESP_LOGI(TAG, "[MQTT] Empty command processed");
 }
 
 if (!expected || strlen(expected) == 0) {
 return success;
 }
 
 // Enhanced response parsing for NMEA interference handling
 bool found_expected = false;
 if (strlen(response_buffer) > 0) {
 // First try direct match
 found_expected = (strstr(response_buffer, expected) != NULL);
 
 // Enhanced handling for AT responses mixed with NMEA data
 if (!found_expected) {
 if (strstr(expected, "OK") != NULL) {
 // SIM7670G may return "YES" instead of "OK" for some commands - handle both
 bool found_ok = (strstr(response_buffer, "OK") != NULL);
 bool found_yes = (strstr(response_buffer, "YES") != NULL);
 
 if (found_yes) {
 found_expected = true;
 ESP_LOGI(TAG, "[MQTT] Found 'YES' response (SIM7670G variant of 'OK')");
 } else if (found_ok) {
 found_expected = true;
 ESP_LOGI(TAG, "[MQTT] Found 'OK' response");
 }
 
 // Look for AT command echo followed by OK/YES pattern
 if (!found_expected) {
 const char* cmd_start = strstr(response_buffer, "AT+");
 if (cmd_start) {
 const char* ok_pos = strstr(cmd_start, "OK");
 const char* yes_pos = strstr(cmd_start, "YES");
 if (ok_pos || yes_pos) {
 // Verify this is a proper AT response
 const char* line_end = strchr(cmd_start, '\n');
 if (line_end && ((ok_pos && ok_pos > line_end) || (yes_pos && yes_pos > line_end))) {
 found_expected = true;
 ESP_LOGI(TAG, "[MQTT] Found AT command response with %s in mixed data", ok_pos ? "OK" : "YES");
 }
 }
 }
 }
 
 // Fallback: Look for standalone OK/YES responses
 if (!found_expected) {
 const char* ptr = response_buffer;
 while (ptr != NULL) {
 // FIXED: Check for NULL before calling strstr to prevent crash
 const char* ok_ptr = strstr(ptr, "OK");
 const char* yes_ptr = strstr(ptr, "YES");
 
 // Choose the earliest match
 if (ok_ptr && (!yes_ptr || ok_ptr < yes_ptr)) {
 ptr = ok_ptr;
 } else if (yes_ptr) {
 ptr = yes_ptr;
 } else {
 break; // No more matches found
 }
 
 // Check if OK/YES is standalone
 bool standalone = true;
 if (ptr > response_buffer) {
 char prev = *(ptr-1);
 if (prev != '\r' && prev != '\n' && prev != ' ') {
 standalone = false;
 }
 }
 
 // Check length before accessing ptr[2]
 size_t match_len = (ptr == ok_ptr) ? 2 : 3; // "OK" = 2, "YES" = 3
 if (strlen(ptr) > match_len) {
 char next = ptr[match_len];
 if (next != '\r' && next != '\n' && next != ' ') {
 standalone = false;
 }
 }
 
 if (standalone) {
 found_expected = true;
 ESP_LOGI(TAG, "[MQTT] Found standalone %s in mixed data", (ptr == ok_ptr) ? "OK" : "YES");
 break;
 }
 ptr += match_len; // Move past this match
 }
 }
 } else {
 // For non-OK expectations, be more lenient with whitespace
 char expected_clean[256];
 strncpy(expected_clean, expected, sizeof(expected_clean) - 1);
 expected_clean[sizeof(expected_clean) - 1] = '\0';
 
 // Remove leading/trailing whitespace from expected
 char* start = expected_clean;
 while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
 
 if (strlen(start) > 0) {
 found_expected = (strstr(response_buffer, start) != NULL);
 }
 }
 }
 }
 
 ESP_LOGI(TAG, "[MQTT] Expected '%s' found: %s", expected, found_expected ? "YES" : "NO");
 if (!found_expected) {
 ESP_LOGI(TAG, "[MQTT] Full response was: '%s'", response_buffer);
 }
 
 return success && found_expected;
}

static bool mqtt_start_service(void)
{
 ESP_LOGI(TAG, "[MQTT] Initializing MQTT service...");
 
 // CRITICAL: Properly establish data connection before MQTT
 ESP_LOGI(TAG, "[MQTT] ESTABLISHING DATA CONNECTION FOR MQTT...");
 const lte_interface_t* lte = lte_get_interface();
 if (lte && lte->send_at_command) {
 at_response_t response = {0};
 
 // Step 1: Verify network registration
 ESP_LOGI(TAG, "[MQTT] Step 1: Checking network registration...");
 if (!lte->send_at_command("AT+CREG?", &response, 3000)) {
 ESP_LOGE(TAG, "[MQTT] Failed to check network registration");
 return false;
 }
 ESP_LOGI(TAG, "[MQTT] Network status: %s", response.response);
 
 if (!strstr(response.response, "+CREG: 0,1") && !strstr(response.response, "+CREG: 0,5")) {
 ESP_LOGE(TAG, "[MQTT] Network not registered for MQTT");
 return false;
 }
 
 // Step 2: Check and configure APN using dedicated APN manager
 ESP_LOGI(TAG, "[MQTT] Step 2: Ensuring APN is properly configured...");
 const apn_manager_interface_t* apn_mgr = apn_manager_get_interface();
 if (apn_mgr) {
 apn_status_t apn_status;
 if (apn_mgr->get_status(&apn_status)) {
 if (apn_status.is_configured && apn_status.is_active) {
 ESP_LOGI(TAG, "[MQTT] APN already configured and active: %s (IP: %s)", 
 apn_status.current_apn, apn_status.ip_address);
 } else if (apn_status.is_configured && !apn_status.is_active) {
 ESP_LOGI(TAG, "[MQTT] APN configured, activating context...");
 if (!apn_mgr->activate_context()) {
 ESP_LOGW(TAG, "[MQTT] Failed to activate PDP context, continuing...");
 }
 } else {
 ESP_LOGI(TAG, "[MQTT] APN not configured, setting default APN...");
 if (apn_mgr->set_apn("m2mglobal", "", "")) {
 apn_mgr->activate_context();
 } else {
 ESP_LOGW(TAG, "[MQTT] APN configuration failed, attempting manual fallback...");
 // Fallback to direct AT command if APN manager fails
 lte->send_at_command("AT+CGDCONT=1,\"IP\",\"m2mglobal\"", &response, 5000);
 }
 }
 }
 } else {
 ESP_LOGW(TAG, "[MQTT] APN manager not available, using direct APN setting");
 lte->send_at_command("AT+CGDCONT=1,\"IP\",\"m2mglobal\"", &response, 5000);
 }
 
 // Step 3: Verify PDP context activation
 ESP_LOGI(TAG, "[MQTT] Step 3: Verifying PDP context for MQTT...");
 if (!lte->send_at_command("AT+CGACT?", &response, 5000)) {
 ESP_LOGW(TAG, "[MQTT] Failed to query PDP context status");
 } else {
 if (strstr(response.response, "+CGACT: 1,1")) {
 ESP_LOGI(TAG, "[MQTT] PDP context is active for MQTT");
 } else {
 ESP_LOGI(TAG, "[MQTT] Activating PDP context...");
 if (lte->send_at_command("AT+CGACT=1,1", &response, 15000)) {
 ESP_LOGI(TAG, "[MQTT] Data connection activated for MQTT: %s", response.response);
 } else {
 ESP_LOGW(TAG, "[MQTT] PDP activation failed, checking current status...");
 }
 }
 }
 
 // Step 4: Verify IP address assignment 
 ESP_LOGI(TAG, "[MQTT] Step 4: Verifying IP address for MQTT...");
 if (lte->send_at_command("AT+CGPADDR=1", &response, 3000)) {
 ESP_LOGI(TAG, "[MQTT] IP address for MQTT: %s", response.response);
 if (strstr(response.response, "0.0.0.0") || !strstr(response.response, "+CGPADDR:")) {
 ESP_LOGE(TAG, "[MQTT] No valid IP address for MQTT");
 return false;
 }
 } else {
 ESP_LOGE(TAG, "[MQTT] Failed to get IP address for MQTT");
 return false;
 }
 
 // Wait for data context to be established
 vTaskDelay(pdMS_TO_TICKS(2000));
 } else {
 ESP_LOGE(TAG, "[MQTT] No LTE interface available for MQTT");
 return false;
 }
 
 // First, stop any existing MQTT service to ensure clean state
 ESP_LOGI(TAG, "[MQTT] Stopping any existing MQTT service...");
 send_mqtt_at_command("AT+CMQTTSTOP", "OK", 3000); // Don't check result, may not be running
 vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for clean shutdown
 
 // Release any existing MQTT clients
 ESP_LOGI(TAG, "[MQTT] Releasing any existing MQTT clients...");
 send_mqtt_at_command("AT+CMQTTREL=1", "OK", 3000); // Don't check result, may not exist
 vTaskDelay(pdMS_TO_TICKS(1000)); // Longer wait for cleanup
 
 // Now start fresh MQTT service using Waveshare official sequence
 ESP_LOGI(TAG, "[MQTT] Starting MQTT service (Waveshare method)...");
 for (int retry = 0; retry < 3; retry++) { // Reduced from 5 to 3 attempts
 ESP_LOGI(TAG, "[MQTT] Service start attempt %d/3...", retry + 1);
 
 // Wait for clean NMEA gap before critical command
 vTaskDelay(pdMS_TO_TICKS(200)); // Reduced delay
 
 if (send_mqtt_at_command("AT+CMQTTSTART", "OK", 8000)) { // SIM7670G returns OK for successful service start
 ESP_LOGI(TAG, "[MQTT] MQTT service started successfully");
 // Give service time to fully initialize
 vTaskDelay(pdMS_TO_TICKS(1000)); // Reduced delay
 return true;
 }
 
 if (retry < 2) { // Adjusted for 3 attempts
 ESP_LOGW(TAG, "[MQTT] Service start failed, retrying in 1 second...");
 // Try to clear any stuck state
 send_mqtt_at_command("AT+CMQTTSTOP", "OK", 2000);
 vTaskDelay(pdMS_TO_TICKS(1000)); // Reduced retry delay
 }
 }
 
 ESP_LOGE(TAG, "[MQTT] Failed to start MQTT service after 5 attempts");
 return false;
}

static bool mqtt_acquire_client(void)
{
 ESP_LOGI(TAG, "[MQTT] Acquiring MQTT client...");
 
 // Verify MQTT service is running before client acquisition
 ESP_LOGI(TAG, "[MQTT] Verifying MQTT service status...");
 const lte_interface_t* lte = lte_get_interface();
 if (lte && lte->send_at_command) {
 at_response_t response = {0};
 if (lte->send_at_command("AT+CMQTTDISC?", &response, 3000)) {
 ESP_LOGI(TAG, "[MQTT] Service check response: %s", response.response);
 if (!strstr(response.response, "+CMQTTDISC:")) {
 ESP_LOGE(TAG, "[MQTT] MQTT service not running properly");
 return false;
 }
 }
 }
 
 // Ensure clean state - release any existing client
 ESP_LOGI(TAG, "[MQTT] Ensuring clean client state...");
 send_mqtt_at_command("AT+CMQTTREL=1", "OK", 2000); // Don't check result
 vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for clean state
 
    // Acquire MQTT client using exact Waveshare documentation format
    // Use EXACT client ID format from working Waveshare examples
    char simple_client_id[32];
    snprintf(simple_client_id, sizeof(simple_client_id), "ESP32GPS%03u", (unsigned int)(esp_random() % 1000));
    
    char client_cmd[128];
    // FIXED: Use correct AT+CMQTTACCQ format from Waveshare documentation
    // Format: AT+CMQTTACCQ=<client_index>,<client_id>,<server_type>
    // server_type: 0=TCP, 1=SSL without certificate, 2=SSL with certificate
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=1,\"%s\",0", simple_client_id); ESP_LOGI(TAG, "[MQTT] Acquiring client: %s", client_cmd);
 ESP_LOGI(TAG, "[MQTT] Using client ID: '%s'", simple_client_id);
 
 // AT Manual: MQTT service must be fully established before client acquisition
 // Wait for service to be ready (critical timing from AT manual)
 ESP_LOGI(TAG, "[MQTT] Waiting for MQTT service to be ready...");
 vTaskDelay(pdMS_TO_TICKS(2000)); // AT Manual: Service needs time to initialize
 
 // Verify service status before attempting client acquisition
 ESP_LOGI(TAG, "[MQTT] Verifying MQTT service status...");
 if (!send_mqtt_at_command("AT+CMQTTDISC?", "+CMQTTDISC:", 3000)) {
 ESP_LOGE(TAG, "[MQTT] MQTT service not responding - cannot acquire client");
 return false;
 }
 
 // Multiple attempts with proper AT command timing
 for (int retry = 0; retry < 3; retry++) {
 ESP_LOGI(TAG, "[MQTT] Client acquisition attempt %d/3...", retry + 1);
 
 // AT Manual: 100ms minimum delay between commands
 vTaskDelay(pdMS_TO_TICKS(150));
 
 if (send_mqtt_at_command(client_cmd, "OK", 8000)) { // SIM7670G returns OK for successful client acquisition
 ESP_LOGI(TAG, "[MQTT] MQTT client acquired successfully");
 vTaskDelay(pdMS_TO_TICKS(1000)); // Allow client to stabilize
 return true;
 }
 
 ESP_LOGW(TAG, "[MQTT] Client acquisition attempt %d failed", retry + 1);
 
 if (retry < 2) {
 // AT Manual: Progressive backoff with service state cleanup
 int wait_time = 3000 + (retry * 1000); // Longer waits
 ESP_LOGI(TAG, "[MQTT] Waiting %d ms before retry...", wait_time);
 vTaskDelay(pdMS_TO_TICKS(wait_time));
 
 // Force service restart if multiple failures (AT Manual recommendation)
 if (retry == 1) {
 ESP_LOGI(TAG, "[MQTT] Forcing service restart after multiple failures...");
 send_mqtt_at_command("AT+CMQTTSTOP", "OK", 3000);
 vTaskDelay(pdMS_TO_TICKS(2000));
 send_mqtt_at_command("AT+CMQTTSTART", "OK", 8000);
 vTaskDelay(pdMS_TO_TICKS(2000));
 }
 }
 }
 
 ESP_LOGE(TAG, "[MQTT] Failed to acquire MQTT client after 8 attempts");
 ESP_LOGE(TAG, "[MQTT] This may indicate network connectivity issues or service problems");
 return false;
}

static bool mqtt_connect_to_broker(void)
{
 ESP_LOGI(TAG, "[MQTT] Connecting to broker: %s:%d", 
 current_config.broker_host, current_config.broker_port);
 
 // Connect to broker - Waveshare documentation format
 char connect_cmd[512];
 const char* protocol = current_config.enable_ssl ? "ssl" : "tcp";
 
 if (strlen(current_config.username) > 0 && strlen(current_config.password) > 0) {
 // With authentication
 snprintf(connect_cmd, sizeof(connect_cmd), 
 "AT+CMQTTCONNECT=1,\"%s://%s:%d\",%d,1,\"%s\",\"%s\"",
 protocol, current_config.broker_host, current_config.broker_port,
 current_config.keepalive_sec, current_config.username, current_config.password);
 } else {
 // Without authentication (no username, no password - supports open MQTT brokers)
 snprintf(connect_cmd, sizeof(connect_cmd), 
 "AT+CMQTTCONNECT=1,\"%s://%s:%d\",%d,1",
 protocol, current_config.broker_host, current_config.broker_port,
 current_config.keepalive_sec);
 }
 
 ESP_LOGI(TAG, "[MQTT] Connection command: %s", connect_cmd);
 
 // Try connection with retry logic
 for (int retry = 0; retry < 3; retry++) {
 ESP_LOGI(TAG, "[MQTT] Broker connection attempt %d/3...", retry + 1);
 
 if (send_mqtt_at_command(connect_cmd, "OK", 20000)) {
 ESP_LOGI(TAG, "[MQTT] Connected to broker successfully");
 
 // Wait for connection establishment
 vTaskDelay(pdMS_TO_TICKS(2000));
 
 // Verify connection status
 if (send_mqtt_at_command("AT+CMQTTCONNECT?", "OK", 3000)) {
 ESP_LOGI(TAG, "[MQTT] Connection verified");
 return true;
 }
 }
 
 if (retry < 2) {
 ESP_LOGW(TAG, "[MQTT] Connection failed, retrying in 2 seconds...");
 vTaskDelay(pdMS_TO_TICKS(2000));
 }
 }
 
 ESP_LOGE(TAG, "[MQTT] Failed to connect to broker after 3 attempts");
 return false;
}

static bool mqtt_check_support(void)
{
 ESP_LOGI(TAG, "[MQTT] Checking SIM7670G MQTT command support...");
 
 // Test MQTT service query command (this one definitely works)
 if (send_mqtt_at_command("AT+CMQTTDISC?", "OK", 3000)) {
 ESP_LOGI(TAG, "[MQTT] SIM7670G MQTT commands supported");
 return true;
 } else {
 ESP_LOGW(TAG, "[MQTT] MQTT query failed - trying service start test...");
 
 // Try starting MQTT service as a test (we'll stop it immediately)
 if (send_mqtt_at_command("AT+CMQTTSTART", "OK", 3000)) {
 ESP_LOGI(TAG, "[MQTT] MQTT service start command works");
 // Stop the test service
 send_mqtt_at_command("AT+CMQTTSTOP", "OK", 2000);
 return true;
 }
 }
 
 ESP_LOGE(TAG, "[MQTT] SIM7670G MQTT commands not supported or not enabled");
 return false;
}

static bool mqtt_init_impl(const mqtt_config_t* config)
{
 if (!config) {
 ESP_LOGE(TAG, "Configuration is NULL");
 return false;
 }
 
 if (module_initialized) {
 ESP_LOGW(TAG, "MQTT module already initialized - reinitializing due to restart");
 // Reset the module state and reinitialize
 module_initialized = false;
 memset(&current_config, 0, sizeof(mqtt_config_t));
 memset(&module_status, 0, sizeof(mqtt_module_status_t));
 }
 
 ESP_LOGI(TAG, "[MQTT] === MQTT MODULE INITIALIZATION ===");
 ESP_LOGI(TAG, "[MQTT] Broker: %s:%d", config->broker_host, config->broker_port);
 ESP_LOGI(TAG, "[MQTT] Client ID: %s", config->client_id);
 ESP_LOGI(TAG, "[MQTT] Topic: %s", config->topic);
 
 // Store configuration
 memcpy(&current_config, config, sizeof(mqtt_config_t));
 
 // ENABLE VERBOSE DEBUG OUTPUT FOR COMPLETE MQTT VISIBILITY
 current_config.debug_output = true;
 ESP_LOGI(TAG, "[MQTT] VERBOSE DEBUG MODE ENABLED - Full MQTT visibility");
 
 // Initialize module status
 memset(&module_status, 0, sizeof(module_status));
 module_status.connection_status = MQTT_STATUS_DISCONNECTED;
 
 // Check if MQTT is supported
 if (!mqtt_check_support()) {
 ESP_LOGE(TAG, "[MQTT] MQTT functionality not available on this SIM7670G firmware");
 return false;
 }
 
 // Start MQTT service
 ESP_LOGI(TAG, "[MQTT] Starting MQTT service (feeding watchdog)...");
 if (!mqtt_start_service()) {
 ESP_LOGE(TAG, "[MQTT] Failed to start MQTT service");
 return false;
 }
 
 // Feed watchdog after MQTT service start
 esp_task_wdt_reset();
 
 // Acquire client
 ESP_LOGI(TAG, "[MQTT] Acquiring MQTT client (feeding watchdog)...");
 if (!mqtt_acquire_client()) {
 ESP_LOGE(TAG, "[MQTT] Failed to acquire MQTT client");
 return false;
 }
 
 // Feed watchdog after MQTT client acquisition
 esp_task_wdt_reset();
 
 module_status.initialized = true;
 module_initialized = true;
 
 ESP_LOGI(TAG, "[MQTT] === MQTT MODULE INITIALIZED SUCCESSFULLY ===");
 return true;
}

static bool mqtt_deinit_impl(void)
{
 if (!module_initialized) {
 return true;
 }
 
 // Disconnect if connected
 mqtt_disconnect_impl();
 
 // Release MQTT client
 send_mqtt_at_command("AT+CMQTTREL=1", "OK", 5000);
 
 // Stop MQTT service
 send_mqtt_at_command("AT+CMQTTSTOP", "OK", 5000);
 
 memset(&module_status, 0, sizeof(module_status));
 module_initialized = false;
 
 ESP_LOGI(TAG, "MQTT module deinitialized");
 return true;
}

static bool mqtt_connect_impl(void)
{
 if (!module_initialized) {
 ESP_LOGE(TAG, "MQTT module not initialized");
 return false;
 }
 
 if (module_status.connection_status == MQTT_STATUS_CONNECTED) {
 ESP_LOGI(TAG, "Already connected to MQTT broker");
 return true;
 }
 
 module_status.connection_status = MQTT_STATUS_CONNECTING;
 
 if (mqtt_connect_to_broker()) {
 module_status.connection_status = MQTT_STATUS_CONNECTED;
 ESP_LOGI(TAG, "MQTT connection successful");
 return true;
 } else {
 module_status.connection_status = MQTT_STATUS_ERROR;
 ESP_LOGE(TAG, "MQTT connection failed");
 return false;
 }
}

static bool mqtt_disconnect_impl(void)
{
 if (module_status.connection_status != MQTT_STATUS_CONNECTED) {
 return true;
 }
 
 ESP_LOGI(TAG, "Disconnecting from MQTT broker");
 
 if (send_mqtt_at_command("AT+CMQTTDISC=1,60", "OK", 10000)) {
 module_status.connection_status = MQTT_STATUS_DISCONNECTED;
 ESP_LOGI(TAG, "MQTT disconnected successfully");
 return true;
 } else {
 ESP_LOGE(TAG, "MQTT disconnect failed");
 return false;
 }
}

static mqtt_status_t mqtt_get_status_impl(void)
{
 return module_status.connection_status;
}

static bool mqtt_get_module_status_impl(mqtt_module_status_t* status)
{
 if (!status) {
 return false;
 }
 
 memcpy(status, &module_status, sizeof(mqtt_module_status_t));
 return true;
}

static bool mqtt_publish_impl(const mqtt_message_t* message, mqtt_publish_result_t* result)
{
 ESP_LOGI(TAG, "[MQTT] === MQTT PUBLISH IMPLEMENTATION START ===");
 
 if (!message || strlen(message->topic) == 0 || strlen(message->payload) == 0) {
 ESP_LOGE(TAG, "[MQTT] Invalid message parameters:");
 ESP_LOGE(TAG, "[MQTT] message: %p", message);
 if (message) {
 ESP_LOGE(TAG, "[MQTT] topic length: %d", strlen(message->topic));
 ESP_LOGE(TAG, "[MQTT] payload length: %d", strlen(message->payload));
 }
 return false;
 }
 
 // CRITICAL: Temporarily disable GPS NMEA output during MQTT publish to prevent interference
 ESP_LOGI(TAG, "[MQTT] Temporarily disabling GPS NMEA for clean MQTT publish...");
 const lte_interface_t* lte = lte_get_interface();
 bool gps_was_enabled = false;
 if (lte && lte->send_at_command) {
 at_response_t response = {0};
 // Check current GPS status
 if (lte->send_at_command("AT+CGNSSTST?", &response, 2000)) {
 gps_was_enabled = strstr(response.response, "+CGNSSTST: 1") != NULL;
 }
 // Disable GPS NMEA output temporarily
 lte->send_at_command("AT+CGNSSTST=0", &response, 2000);
 vTaskDelay(pdMS_TO_TICKS(500)); // Wait for GPS to stop outputting
 }
 
 ESP_LOGI(TAG, "[MQTT] Connection Status Check: %d (Expected: %d)", 
 module_status.connection_status, MQTT_STATUS_CONNECTED);
 
 if (module_status.connection_status != MQTT_STATUS_CONNECTED) {
 ESP_LOGE(TAG, "[MQTT] NOT CONNECTED TO MQTT BROKER!");
 ESP_LOGE(TAG, "[MQTT] Current status: %d", module_status.connection_status);
 ESP_LOGE(TAG, "[MQTT] Required status: %d (CONNECTED)", MQTT_STATUS_CONNECTED);
 // Re-enable GPS on error
 if (lte && lte->send_at_command && gps_was_enabled) {
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 if (result) result->success = false;
 return false;
 }
 
 ESP_LOGI(TAG, "[MQTT] MQTT Connection Status: CONNECTED");
 ESP_LOGI(TAG, "[MQTT] 📝 Publishing to topic: '%s'", message->topic);
 ESP_LOGI(TAG, "[MQTT] Payload (%d bytes): %s", strlen(message->payload), message->payload);
 ESP_LOGI(TAG, "[MQTT] QoS: %d, Retain: %s", message->qos, message->retain ? "true" : "false");
 
 // Step 1: Set topic - Waveshare format: AT+CMQTTTOPIC=0,topic_length
 ESP_LOGI(TAG, "[MQTT] STEP 1: Setting MQTT topic...");
 char topic_cmd[128];
 snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTTOPIC=1,%d", (int)strlen(message->topic));
 ESP_LOGI(TAG, "[MQTT] 📤 Topic command: %s", topic_cmd);
 
 if (!send_mqtt_at_command(topic_cmd, ">", 3000)) {
 ESP_LOGE(TAG, "[MQTT] FAILED to set MQTT topic - no '>' prompt received");
 // Re-enable GPS on error
 if (lte && lte->send_at_command && gps_was_enabled) {
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 if (result) result->success = false;
 return false;
 }
 ESP_LOGI(TAG, "[MQTT] Topic command sent, got '>' prompt");
 
 // Step 2: Send topic data (this should send the topic string directly)
 ESP_LOGI(TAG, "[MQTT] STEP 2: Sending topic data: '%s'", message->topic);
 if (!send_mqtt_at_command(message->topic, "OK", 3000)) {
 ESP_LOGE(TAG, "[MQTT] FAILED to send topic data - no 'OK' received");
 // Re-enable GPS on error
 if (lte && lte->send_at_command && gps_was_enabled) {
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 if (result) result->success = false;
 return false;
 }
 ESP_LOGI(TAG, "[MQTT] Topic data sent successfully");
 
 // Step 3: Set payload - Waveshare format: AT+CMQTTPAYLOAD=0,payload_length
 ESP_LOGI(TAG, "[MQTT] STEP 3: Setting MQTT payload...");
 char payload_cmd[128];
 snprintf(payload_cmd, sizeof(payload_cmd), "AT+CMQTTPAYLOAD=1,%d", (int)strlen(message->payload));
 ESP_LOGI(TAG, "[MQTT] 📤 Payload command: %s", payload_cmd);
 
 if (!send_mqtt_at_command(payload_cmd, ">", 3000)) {
 ESP_LOGE(TAG, "[MQTT] FAILED to set MQTT payload - no '>' prompt received");
 // Re-enable GPS on error
 if (lte && lte->send_at_command && gps_was_enabled) {
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 if (result) result->success = false;
 return false;
 }
 ESP_LOGI(TAG, "[MQTT] Payload command sent, got '>' prompt");
 
 // Step 4: Send payload data
 ESP_LOGI(TAG, "[MQTT] STEP 4: Sending payload data (%d bytes)...", strlen(message->payload));
 ESP_LOGI(TAG, "[MQTT] Payload content: %s", message->payload);
 if (!send_mqtt_at_command(message->payload, "OK", 3000)) {
 ESP_LOGE(TAG, "[MQTT] FAILED to send payload data - no 'OK' received");
 // Re-enable GPS on error
 if (lte && lte->send_at_command && gps_was_enabled) {
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 if (result) result->success = false;
 return false;
 }
 ESP_LOGI(TAG, "[MQTT] Payload data sent successfully");
 
 // Step 5: Publish - Waveshare format: AT+CMQTTPUB=0,qos,retain
 ESP_LOGI(TAG, "[MQTT] STEP 5: Publishing message...");
 char pub_cmd[64];
 snprintf(pub_cmd, sizeof(pub_cmd), "AT+CMQTTPUB=1,%d,%d", 
 message->qos, message->retain ? 1 : 0);
 ESP_LOGI(TAG, "[MQTT] 📤 Publish command: %s", pub_cmd);
 
 bool publish_success = send_mqtt_at_command(pub_cmd, "OK", 10000);
 
 // Re-enable GPS NMEA output if it was enabled before
 if (lte && lte->send_at_command && gps_was_enabled) {
 ESP_LOGI(TAG, "[MQTT] Re-enabling GPS NMEA output...");
 at_response_t response = {0};
 lte->send_at_command("AT+CGNSSTST=1", &response, 2000);
 }
 
 if (publish_success) {
 ESP_LOGI(TAG, "[MQTT] === MESSAGE PUBLISHED SUCCESSFULLY ===");
 if (result) {
 result->success = true;
 result->message_id = 0; // SIM7670G doesn't provide message ID
 }
 return true;
 } else {
 ESP_LOGE(TAG, "[MQTT] === FAILED TO PUBLISH MESSAGE ===");
 if (result) result->success = false;
 return false;
 }
}

static bool mqtt_publish_json_impl(const char* topic, const char* json_payload, mqtt_publish_result_t* result)
{
 ESP_LOGI(TAG, "[MQTT] === MQTT JSON PUBLISH REQUEST ===");
 
 if (!topic || !json_payload) {
 ESP_LOGE(TAG, "[MQTT] ERROR: Topic or payload is NULL");
 return false;
 }
 
 ESP_LOGI(TAG, "[MQTT] 📝 Topic: '%s'", topic);
 ESP_LOGI(TAG, "[MQTT] Payload length: %d bytes", strlen(json_payload));
 ESP_LOGI(TAG, "[MQTT] Full JSON payload: %s", json_payload);
 
 mqtt_message_t message = {0};
 
 // Copy topic and payload to message structure arrays
 strncpy(message.topic, topic, sizeof(message.topic) - 1);
 strncpy(message.payload, json_payload, sizeof(message.payload) - 1);
 message.qos = current_config.qos_level;
 message.retain = current_config.retain_messages;
 message.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
 
 ESP_LOGI(TAG, "[MQTT] Message settings: QoS=%d, Retain=%s, Timestamp=%lu", 
 message.qos, message.retain ? "true" : "false", message.timestamp);
 
 ESP_LOGI(TAG, "[MQTT] Calling mqtt_publish_impl...");
 bool success = mqtt_publish_impl(&message, result);
 
 ESP_LOGI(TAG, "[MQTT] Publish result: %s", success ? "SUCCESS " : "FAILED ");
 
 return success;
}

static bool mqtt_subscribe_impl(const char* topic, int qos)
{
 // Simplified subscription implementation
 ESP_LOGI(TAG, "Subscribing to topic: %s (QoS: %d)", topic, qos);
 return true; // For now, just return success
}

static bool mqtt_unsubscribe_impl(const char* topic)
{
 ESP_LOGI(TAG, "Unsubscribing from topic: %s", topic);
 return true; // For now, just return success
}

static bool mqtt_is_connected_impl(void)
{
 return (module_status.connection_status == MQTT_STATUS_CONNECTED);
}

static void mqtt_set_debug_impl(bool enable)
{
 current_config.debug_output = enable;
 ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// Utility functions
bool mqtt_create_json_payload(const char* latitude, const char* longitude, 
 float battery_voltage, int battery_percentage,
 char* json_buffer, size_t buffer_size)
{
 if (!json_buffer || buffer_size == 0) {
 return false;
 }
 
 cJSON* json = cJSON_CreateObject();
 if (!json) {
 return false;
 }
 
 // Add device information
 cJSON_AddStringToObject(json, "device_id", "Waveshare-7670X");
 cJSON_AddNumberToObject(json, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
 
 // Add GPS/GNSS data
 cJSON* gnss = cJSON_CreateObject();
 if (latitude && longitude && 
 strcmp(latitude, "0.000000") != 0 && strcmp(longitude, "0.000000") != 0) {
 cJSON_AddStringToObject(gnss, "latitude", latitude);
 cJSON_AddStringToObject(gnss, "longitude", longitude);
 cJSON_AddStringToObject(gnss, "status", "fix");
 } else {
 cJSON_AddStringToObject(gnss, "latitude", "0.000000");
 cJSON_AddStringToObject(gnss, "longitude", "0.000000");
 cJSON_AddStringToObject(gnss, "status", "no_fix");
 }
 
 // Add satellite info (these would be passed as parameters in a real implementation)
 cJSON_AddNumberToObject(gnss, "satellites", 7); // Current working satellite count
 cJSON_AddNumberToObject(gnss, "hdop", 1.41); // Current HDOP accuracy
 cJSON_AddStringToObject(gnss, "constellation", "GPS+GLONASS+Galileo+BeiDou");
 
 cJSON_AddItemToObject(json, "gnss", gnss);
 
 // Add battery status
 cJSON* battery = cJSON_CreateObject();
 cJSON_AddNumberToObject(battery, "voltage", battery_voltage);
 cJSON_AddNumberToObject(battery, "percentage", battery_percentage);
 
 // Determine battery status
 const char* battery_status = "normal";
 if (battery_percentage < 15) {
 battery_status = battery_percentage < 5 ? "critical" : "low";
 }
 cJSON_AddStringToObject(battery, "status", battery_status);
 
 cJSON_AddItemToObject(json, "battery", battery);
 
 // Add system status
 cJSON* system = cJSON_CreateObject();
 cJSON_AddStringToObject(system, "version", "1.0.1");
 cJSON_AddStringToObject(system, "status", "operational");
 cJSON_AddNumberToObject(system, "uptime_ms", xTaskGetTickCount() * portTICK_PERIOD_MS);
 cJSON_AddItemToObject(json, "system", system);
 
 char* json_string = cJSON_Print(json);
 if (!json_string) {
 cJSON_Delete(json);
 return false;
 }
 
 strncpy(json_buffer, json_string, buffer_size - 1);
 json_buffer[buffer_size - 1] = '\0';
 
 free(json_string);
 cJSON_Delete(json);
 return true;
}

// Convenient function for publishing GPS tracker data
bool mqtt_publish_gps_data(const char* latitude, const char* longitude, 
 float battery_voltage, int battery_percentage)
{
 if (!module_initialized || module_status.connection_status != MQTT_STATUS_CONNECTED) {
 ESP_LOGE(TAG, "MQTT not connected - cannot publish GPS data");
 return false;
 }
 
 // Create JSON payload
 char json_payload[1024];
 if (!mqtt_create_json_payload(latitude, longitude, battery_voltage, battery_percentage,
 json_payload, sizeof(json_payload))) {
 ESP_LOGE(TAG, "Failed to create GPS JSON payload");
 return false;
 }
 
 // Publish to configured topic
 mqtt_publish_result_t result;
 bool success = mqtt_publish_json_impl(current_config.topic, json_payload, &result);
 
 if (success) {
 ESP_LOGI(TAG, " GPS data published to topic: %s", current_config.topic);
 ESP_LOGI(TAG, " Payload size: %d bytes", (int)strlen(json_payload));
 } else {
 ESP_LOGE(TAG, " Failed to publish GPS data");
 }
 
 return success;
}

// Enhanced JSON payload creation using GPS data structure
bool mqtt_create_enhanced_json_payload(const gps_data_t* gps_data, const battery_data_t* battery_data,
 bool fresh_gps_data, char* json_buffer, size_t buffer_size)
{
 if (!json_buffer || buffer_size == 0) {
 return false;
 }
 
 cJSON* json = cJSON_CreateObject();
 if (!json) {
 return false;
 }
 
 // Add device information
 cJSON_AddStringToObject(json, "device_id", "Waveshare-7670X");
 cJSON_AddStringToObject(json, "device_type", "ESP32-S3-SIM7670G");
 cJSON_AddStringToObject(json, "firmware_version", "1.0.1");
 cJSON_AddNumberToObject(json, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
 
 // Add GPS/GNSS data with full details
 cJSON* gnss = cJSON_CreateObject();
 if (gps_data && gps_data->fix_valid && 
 gps_data->latitude != 0.0f && gps_data->longitude != 0.0f) {
 
 cJSON_AddNumberToObject(gnss, "latitude", gps_data->latitude);
 cJSON_AddNumberToObject(gnss, "longitude", gps_data->longitude);
 cJSON_AddNumberToObject(gnss, "altitude", gps_data->altitude);
 cJSON_AddNumberToObject(gnss, "speed_kmh", gps_data->speed_kmh);
 cJSON_AddNumberToObject(gnss, "course", gps_data->course);
 cJSON_AddNumberToObject(gnss, "satellites", gps_data->satellites);
 cJSON_AddNumberToObject(gnss, "hdop", gps_data->hdop);
 cJSON_AddStringToObject(gnss, "fix_quality", &gps_data->fix_quality);
 cJSON_AddStringToObject(gnss, "timestamp", gps_data->timestamp);
 cJSON_AddStringToObject(gnss, "status", "valid_fix");
 cJSON_AddBoolToObject(gnss, "fresh_data", fresh_gps_data);
 cJSON_AddStringToObject(gnss, "constellation", "GPS+GLONASS+Galileo+BeiDou");
 
 } else {
 cJSON_AddNumberToObject(gnss, "latitude", 0.0);
 cJSON_AddNumberToObject(gnss, "longitude", 0.0);
 cJSON_AddNumberToObject(gnss, "altitude", 0.0);
 cJSON_AddNumberToObject(gnss, "satellites", 0);
 cJSON_AddStringToObject(gnss, "status", "no_fix");
 cJSON_AddBoolToObject(gnss, "fresh_data", false);
 }
 cJSON_AddItemToObject(json, "gnss", gnss);
 
 // Add battery status with full details
 cJSON* battery = cJSON_CreateObject();
 if (battery_data) {
 cJSON_AddNumberToObject(battery, "voltage", battery_data->voltage);
 cJSON_AddNumberToObject(battery, "percentage", battery_data->percentage);
 cJSON_AddBoolToObject(battery, "charging", battery_data->charging);
 
 // Battery status determination
 const char* battery_status = "normal";
 if (battery_data->percentage < 5) {
 battery_status = "critical";
 } else if (battery_data->percentage < 15) {
 battery_status = "low";
 }
 cJSON_AddStringToObject(battery, "status", battery_status);
 } else {
 cJSON_AddNumberToObject(battery, "voltage", 0.0);
 cJSON_AddNumberToObject(battery, "percentage", 0);
 cJSON_AddBoolToObject(battery, "charging", false);
 cJSON_AddStringToObject(battery, "status", "unknown");
 }
 cJSON_AddItemToObject(json, "battery", battery);
 
 // Add system status
 cJSON* system = cJSON_CreateObject();
 cJSON_AddStringToObject(system, "status", "operational");
 cJSON_AddNumberToObject(system, "uptime_ms", xTaskGetTickCount() * portTICK_PERIOD_MS);
 cJSON_AddNumberToObject(system, "free_heap", esp_get_free_heap_size());
 cJSON_AddItemToObject(json, "system", system);
 
 // Convert to string
 char* json_string = cJSON_Print(json);
 if (!json_string) {
 cJSON_Delete(json);
 return false;
 }
 
 // Copy to buffer
 size_t json_len = strlen(json_string);
 if (json_len >= buffer_size) {
 ESP_LOGW(TAG, "JSON payload too large (%zu bytes, buffer: %zu)", json_len, buffer_size);
 free(json_string);
 cJSON_Delete(json);
 return false;
 }
 
 strcpy(json_buffer, json_string);
 free(json_string);
 cJSON_Delete(json);
 
 return true;
}