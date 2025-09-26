#include "mqtt_module.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "cJSON.h"
#include "time.h"
#include "sys/time.h"
// Include LTE module for shared AT interface
#include "../lte/lte_module.h"

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
    
    ESP_LOGI(TAG, "[MQTT] AT CMD: %s", command);
    
    // Use LTE module's AT command interface for MQTT commands
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "[MQTT] LTE module AT interface not available");
        return false;
    }
    
    at_response_t response = {0};
    bool success = false;
    
    if (strlen(command) > 0) {
        // Add small delay before AT command to avoid NMEA interference
        vTaskDelay(pdMS_TO_TICKS(100));
        
        success = lte->send_at_command(command, &response, timeout_ms);
        ESP_LOGI(TAG, "[MQTT] AT RESP: %s (success: %s)", 
                 response.response, success ? "YES" : "NO");
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
    if (strlen(response.response) > 0) {
        // First try direct match
        found_expected = (strstr(response.response, expected) != NULL);
        
        // Enhanced handling for AT responses mixed with NMEA data
        if (!found_expected) {
            if (strstr(expected, "OK") != NULL) {
                // Look for AT command echo followed by OK pattern
                const char* cmd_start = strstr(response.response, "AT+");
                if (cmd_start) {
                    const char* ok_pos = strstr(cmd_start, "OK");
                    if (ok_pos) {
                        // Verify this is a proper AT response
                        const char* line_end = strchr(cmd_start, '\n');
                        if (line_end && ok_pos > line_end) {
                            found_expected = true;
                            ESP_LOGI(TAG, "[MQTT] Found AT command response with OK in mixed data");
                        }
                    }
                }
                
                // Fallback: Look for standalone OK responses
                if (!found_expected) {
                    const char* ptr = response.response;
                    while ((ptr = strstr(ptr, "OK")) != NULL) {
                        // Check if OK is standalone
                        bool standalone = true;
                        if (ptr > response.response) {
                            char prev = *(ptr-1);
                            if (prev != '\r' && prev != '\n' && prev != ' ') {
                                standalone = false;
                            }
                        }
                        if (ptr[2] != '\0') {
                            char next = ptr[2];
                            if (next != '\r' && next != '\n' && next != ' ') {
                                standalone = false;
                            }
                        }
                        if (standalone) {
                            found_expected = true;
                            ESP_LOGI(TAG, "[MQTT] Found standalone OK in mixed data");
                            break;
                        }
                        ptr += 2;
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
                    found_expected = (strstr(response.response, start) != NULL);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "[MQTT] Expected '%s' found: %s", expected, found_expected ? "YES" : "NO");
    
    return success && found_expected;
}

static bool mqtt_start_service(void)
{
    ESP_LOGI(TAG, "[MQTT] Initializing MQTT service...");
    
    // First, stop any existing MQTT service to ensure clean state
    ESP_LOGI(TAG, "[MQTT] Stopping any existing MQTT service...");
    send_mqtt_at_command("AT+CMQTTSTOP", "OK", 3000);  // Don't check result, may not be running
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for clean shutdown
    
    // Release any existing MQTT clients
    ESP_LOGI(TAG, "[MQTT] Releasing any existing MQTT clients...");
    send_mqtt_at_command("AT+CMQTTREL=0", "OK", 3000);  // Don't check result, may not exist
    vTaskDelay(pdMS_TO_TICKS(500));   // Brief wait
    
    // Now start fresh MQTT service using Waveshare official sequence
    ESP_LOGI(TAG, "[MQTT] Starting MQTT service (Waveshare method)...");
    for (int retry = 0; retry < 3; retry++) {
        ESP_LOGI(TAG, "[MQTT] Service start attempt %d/3...", retry + 1);
        
        if (send_mqtt_at_command("AT+CMQTTSTART", "OK", 10000)) {
            ESP_LOGI(TAG, "[MQTT] ✅ MQTT service started successfully");
            return true;
        }
        
        if (retry < 2) {
            ESP_LOGW(TAG, "[MQTT] Service start failed, retrying in 2 seconds...");
            // Try to clear any stuck state
            send_mqtt_at_command("AT+CMQTTSTOP", "OK", 2000);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    
    ESP_LOGE(TAG, "[MQTT] ❌ Failed to start MQTT service after 3 attempts");
    return false;
}

static bool mqtt_acquire_client(void)
{
    ESP_LOGI(TAG, "[MQTT] Acquiring MQTT client...");
    
    // Ensure clean state - release any existing client
    ESP_LOGI(TAG, "[MQTT] Ensuring clean client state...");
    send_mqtt_at_command("AT+CMQTTREL=0", "OK", 2000);  // Don't check result
    vTaskDelay(pdMS_TO_TICKS(500));  // Wait for clean state
    
    // Acquire MQTT client using exact Waveshare documentation format
    char client_cmd[128];
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\",0", current_config.client_id);
    
    ESP_LOGI(TAG, "[MQTT] Acquiring client: %s", client_cmd);
    
    // Multiple attempts with proper delays between NMEA bursts
    for (int retry = 0; retry < 5; retry++) {
        ESP_LOGI(TAG, "[MQTT] Client acquisition attempt %d/5...", retry + 1);
        
        // Wait for NMEA gap - important for command success
        vTaskDelay(pdMS_TO_TICKS(300));
        
        if (send_mqtt_at_command(client_cmd, "OK", 8000)) {
            ESP_LOGI(TAG, "[MQTT] ✅ MQTT client acquired successfully");
            return true;
        }
        
        ESP_LOGW(TAG, "[MQTT] Client acquisition attempt %d failed", retry + 1);
        
        if (retry < 4) {
            // Wait longer between attempts to avoid NMEA interference
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
    
    ESP_LOGE(TAG, "[MQTT] ❌ Failed to acquire MQTT client after 5 attempts");
    ESP_LOGE(TAG, "[MQTT] This may indicate SIM7670G MQTT service incompatibility");
    return false;
}

static bool mqtt_connect_to_broker(void)
{
    ESP_LOGI(TAG, "[MQTT] Connecting to broker: %s:%d", 
             current_config.broker_host, current_config.broker_port);
    
    // Connect to broker - Waveshare documentation format
    char connect_cmd[512];
    if (strlen(current_config.username) > 0 && strlen(current_config.password) > 0) {
        // With authentication
        snprintf(connect_cmd, sizeof(connect_cmd), 
                "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1,\"%s\",\"%s\"",
                current_config.broker_host, current_config.broker_port,
                current_config.keepalive_sec, current_config.username, current_config.password);
    } else {
        // Without authentication (your case: no username, no password)
        snprintf(connect_cmd, sizeof(connect_cmd), 
                "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1",
                current_config.broker_host, current_config.broker_port,
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
        ESP_LOGI(TAG, "[MQTT] ✅ SIM7670G MQTT commands supported");
        return true;
    } else {
        ESP_LOGW(TAG, "[MQTT] ⚠️ MQTT query failed - trying service start test...");
        
        // Try starting MQTT service as a test (we'll stop it immediately)
        if (send_mqtt_at_command("AT+CMQTTSTART", "OK", 3000)) {
            ESP_LOGI(TAG, "[MQTT] ✅ MQTT service start command works");
            // Stop the test service
            send_mqtt_at_command("AT+CMQTTSTOP", "OK", 2000);
            return true;
        }
    }
    
    ESP_LOGE(TAG, "[MQTT] ❌ SIM7670G MQTT commands not supported or not enabled");
    return false;
}

static bool mqtt_init_impl(const mqtt_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    if (module_initialized) {
        ESP_LOGW(TAG, "MQTT module already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "[MQTT] === MQTT MODULE INITIALIZATION ===");
    ESP_LOGI(TAG, "[MQTT] Broker: %s:%d", config->broker_host, config->broker_port);
    ESP_LOGI(TAG, "[MQTT] Client ID: %s", config->client_id);
    ESP_LOGI(TAG, "[MQTT] Topic: %s", config->topic);
    
    // Store configuration
    memcpy(&current_config, config, sizeof(mqtt_config_t));
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.connection_status = MQTT_STATUS_DISCONNECTED;
    
    // Check if MQTT is supported
    if (!mqtt_check_support()) {
        ESP_LOGE(TAG, "[MQTT] MQTT functionality not available on this SIM7670G firmware");
        return false;
    }
    
    // Start MQTT service
    if (!mqtt_start_service()) {
        ESP_LOGE(TAG, "[MQTT] Failed to start MQTT service");
        return false;
    }
    
    // Acquire client
    if (!mqtt_acquire_client()) {
        ESP_LOGE(TAG, "[MQTT] Failed to acquire MQTT client");
        return false;
    }
    
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
    send_mqtt_at_command("AT+CMQTTREL=0", "OK", 5000);
    
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
    
    if (send_mqtt_at_command("AT+CMQTTDISC=0,60", "OK", 10000)) {
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
    if (!message || strlen(message->topic) == 0 || strlen(message->payload) == 0) {
        ESP_LOGE(TAG, "Invalid message parameters");
        return false;
    }
    
    if (module_status.connection_status != MQTT_STATUS_CONNECTED) {
        ESP_LOGE(TAG, "Not connected to MQTT broker");
        if (result) result->success = false;
        return false;
    }
    
    ESP_LOGI(TAG, "[MQTT] Publishing to topic: %s", message->topic);
    ESP_LOGI(TAG, "[MQTT] Payload: %s", message->payload);
    
    // Set topic - Waveshare format: AT+CMQTTTOPIC=0,topic_length
    char topic_cmd[128];
    snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTTOPIC=0,%d", (int)strlen(message->topic));
    
    if (!send_mqtt_at_command(topic_cmd, ">", 3000)) {
        ESP_LOGE(TAG, "Failed to set MQTT topic");
        if (result) result->success = false;
        return false;
    }
    
    // Send topic data (this should send the topic string directly)
    if (!send_mqtt_at_command(message->topic, "OK", 3000)) {
        ESP_LOGE(TAG, "Failed to send topic data");
        if (result) result->success = false;
        return false;
    }
    
    // Set payload - Waveshare format: AT+CMQTTPAYLOAD=0,payload_length
    char payload_cmd[128];
    snprintf(payload_cmd, sizeof(payload_cmd), "AT+CMQTTPAYLOAD=0,%d", (int)strlen(message->payload));
    
    if (!send_mqtt_at_command(payload_cmd, ">", 3000)) {
        ESP_LOGE(TAG, "Failed to set MQTT payload");
        if (result) result->success = false;
        return false;
    }
    
    // Send payload data
    if (!send_mqtt_at_command(message->payload, "OK", 3000)) {
        ESP_LOGE(TAG, "Failed to send payload data");
        if (result) result->success = false;
        return false;
    }
    
    // Publish - Waveshare format: AT+CMQTTPUB=0,qos,retain
    char pub_cmd[64];
    snprintf(pub_cmd, sizeof(pub_cmd), "AT+CMQTTPUB=0,%d,%d", 
             message->qos, message->retain ? 1 : 0);
    
    if (send_mqtt_at_command(pub_cmd, "OK", 10000)) {
        ESP_LOGI(TAG, "[MQTT] Message published successfully");
        if (result) {
            result->success = true;
            result->message_id = 0; // SIM7670G doesn't provide message ID
        }
        return true;
    } else {
        ESP_LOGE(TAG, "[MQTT] Failed to publish message");
        if (result) result->success = false;
        return false;
    }
}

// Helper function to create GPS tracker JSON payload
static bool mqtt_create_tracker_payload(char* json_buffer, size_t buffer_size, 
                                       double latitude, double longitude, 
                                       float battery_voltage, float battery_percentage)
{
    if (!json_buffer || buffer_size == 0) {
        return false;
    }
    
    // Create JSON payload for GPS tracker
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return false;
    }
    
    // Add timestamp (Unix timestamp)
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(json, "timestamp", (double)now);
    
    // Add GPS data
    cJSON *gps = cJSON_CreateObject();
    if (gps) {
        cJSON_AddNumberToObject(gps, "latitude", latitude);
        cJSON_AddNumberToObject(gps, "longitude", longitude);
        cJSON_AddStringToObject(gps, "fix_quality", "GPS");
        cJSON_AddItemToObject(json, "gps", gps);
    }
    
    // Add battery data
    cJSON *battery = cJSON_CreateObject();
    if (battery) {
        cJSON_AddNumberToObject(battery, "voltage", battery_voltage);
        cJSON_AddNumberToObject(battery, "percentage", battery_percentage);
        cJSON_AddStringToObject(battery, "status", "normal");
        cJSON_AddItemToObject(json, "battery", battery);
    }
    
    // Add device info
    cJSON *device = cJSON_CreateObject();
    if (device) {
        cJSON_AddStringToObject(device, "id", current_config.client_id);
        cJSON_AddStringToObject(device, "type", "esp32_gps_tracker");
        cJSON_AddStringToObject(device, "firmware_version", "1.0.0");
        cJSON_AddItemToObject(json, "device", device);
    }
    
    // Convert to string
    char *json_string = cJSON_Print(json);
    if (json_string) {
        size_t json_len = strlen(json_string);
        if (json_len < buffer_size) {
            strncpy(json_buffer, json_string, buffer_size - 1);
            json_buffer[buffer_size - 1] = '\0';
            free(json_string);
            cJSON_Delete(json);
            return true;
        }
        free(json_string);
    }
    
    cJSON_Delete(json);
    ESP_LOGE(TAG, "JSON payload too large for buffer");
    return false;
}

static bool mqtt_publish_json_impl(const char* topic, const char* json_payload, mqtt_publish_result_t* result)
{
    if (!topic || !json_payload) {
        ESP_LOGE(TAG, "Topic or payload is NULL");
        return false;
    }
    
    mqtt_message_t message = {0};
    
    // Copy topic and payload to message structure arrays
    strncpy(message.topic, topic, sizeof(message.topic) - 1);
    strncpy(message.payload, json_payload, sizeof(message.payload) - 1);
    message.qos = current_config.qos_level;
    message.retain = current_config.retain_messages;
    message.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    return mqtt_publish_impl(&message, result);
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
    cJSON_AddStringToObject(json, "device_id", "esp32_gps_tracker_dev");
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
    cJSON_AddNumberToObject(gnss, "satellites", 7);  // Current working satellite count
    cJSON_AddNumberToObject(gnss, "hdop", 1.41);     // Current HDOP accuracy
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
        ESP_LOGI(TAG, "✅ GPS data published to topic: %s", current_config.topic);
        ESP_LOGI(TAG, "📡 Payload size: %d bytes", (int)strlen(json_payload));
    } else {
        ESP_LOGE(TAG, "❌ Failed to publish GPS data");
    }
    
    return success;
}