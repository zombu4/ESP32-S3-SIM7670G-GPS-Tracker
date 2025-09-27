#include "mqtt_module.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "cJSON.h"
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
    
    // Check if response contains expected text
    bool found_expected = (strstr(response.response, expected) != NULL);
    ESP_LOGI(TAG, "[MQTT] Expected '%s' found: %s", expected, found_expected ? "YES" : "NO");
    
    return success && found_expected;
}

static bool mqtt_start_service(void)
{
    ESP_LOGI(TAG, "[MQTT] Starting MQTT service...");
    
    // Check if already running
    if (send_mqtt_at_command("AT+CMQTTDISC?", "OK", 2000)) {
        ESP_LOGI(TAG, "[MQTT] MQTT service already running, stopping first...");
        send_mqtt_at_command("AT+CMQTTSTOP", "OK", 5000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Start MQTT service - Waveshare documentation
    if (!send_mqtt_at_command("AT+CMQTTSTART", "OK", 5000)) {
        ESP_LOGE(TAG, "[MQTT] Failed to start MQTT service");
        return false;
    }
    
    ESP_LOGI(TAG, "[MQTT] MQTT service started successfully");
    return true;
}

static bool mqtt_acquire_client(void)
{
    ESP_LOGI(TAG, "[MQTT] Acquiring MQTT client...");
    
    // Acquire MQTT client - Waveshare documentation format: AT+CMQTTACCQ=0,"client_id",0
    char client_cmd[128];
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\",0", current_config.client_id);
    
    if (!send_mqtt_at_command(client_cmd, "OK", 5000)) {
        ESP_LOGE(TAG, "[MQTT] Failed to acquire MQTT client");
        return false;
    }
    
    ESP_LOGI(TAG, "[MQTT] MQTT client acquired successfully");
    return true;
}

static bool mqtt_connect_to_broker(void)
{
    ESP_LOGI(TAG, "[MQTT] Connecting to broker: %s:%d", 
             current_config.broker_host, current_config.broker_port);
    
    // Connect to broker - Waveshare documentation format
    char connect_cmd[256];
    if (strlen(current_config.username) > 0) {
        snprintf(connect_cmd, sizeof(connect_cmd), 
                "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1,\"%s\",\"%s\"",
                current_config.broker_host, current_config.broker_port,
                current_config.keepalive_sec, current_config.username, current_config.password);
    } else {
        snprintf(connect_cmd, sizeof(connect_cmd), 
                "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,1",
                current_config.broker_host, current_config.broker_port,
                current_config.keepalive_sec);
    }
    
    if (!send_mqtt_at_command(connect_cmd, "OK", 15000)) {
        ESP_LOGE(TAG, "[MQTT] Failed to connect to broker");
        return false;
    }
    
    ESP_LOGI(TAG, "[MQTT] Connected to broker successfully");
    return true;
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
    
    // Start MQTT service
    if (!mqtt_start_service()) {
        return false;
    }
    
    // Acquire client
    if (!mqtt_acquire_client()) {
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
    if (!message || !message->topic || !message->payload) {
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

static bool mqtt_publish_json_impl(const char* topic, const char* json_payload, mqtt_publish_result_t* result)
{
    mqtt_message_t message = {
        .topic = topic,
        .payload = json_payload,
        .qos = current_config.qos_level,
        .retain = current_config.retain_messages
    };
    
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
    
    // Add location data
    if (latitude && longitude) {
        cJSON* location = cJSON_CreateObject();
        cJSON_AddStringToObject(location, "lat", latitude);
        cJSON_AddStringToObject(location, "lon", longitude);
        cJSON_AddItemToObject(json, "location", location);
    }
    
    // Add battery data
    cJSON* battery = cJSON_CreateObject();
    cJSON_AddNumberToObject(battery, "voltage", battery_voltage);
    cJSON_AddNumberToObject(battery, "percentage", battery_percentage);
    cJSON_AddItemToObject(json, "battery", battery);
    
    // Add timestamp
    cJSON_AddNumberToObject(json, "timestamp", xTaskGetTickCount() * portTICK_PERIOD_MS);
    
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