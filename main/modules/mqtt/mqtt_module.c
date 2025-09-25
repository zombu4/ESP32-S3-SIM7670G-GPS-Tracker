#include "mqtt_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "cJSON.h"

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

// Helper functions
static bool send_mqtt_at_command(const char* command, const char* expected, int timeout_ms);
static bool set_mqtt_topic(const char* topic);
static bool set_mqtt_payload(const char* payload);
static bool publish_mqtt_message(mqtt_publish_result_t* result);

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
    
    // Store configuration
    memcpy(&current_config, config, sizeof(mqtt_config_t));
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.connection_status = MQTT_STATUS_DISCONNECTED;
    
    // Start MQTT service
    if (!send_mqtt_at_command("AT+CMQTTSTART", "OK", 5000)) {
        ESP_LOGE(TAG, "Failed to start MQTT service");
        return false;
    }
    
    // Acquire MQTT client
    char client_cmd[128];
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\",0", config->client_id);
    if (!send_mqtt_at_command(client_cmd, "OK", 5000)) {
        ESP_LOGE(TAG, "Failed to acquire MQTT client");
        return false;
    }
    
    module_status.initialized = true;
    module_initialized = true;
    
    if (config->debug_output) {
        ESP_LOGI(TAG, "MQTT module initialized successfully");
        ESP_LOGI(TAG, "  Broker: %s:%d", config->broker_host, config->broker_port);
        ESP_LOGI(TAG, "  Client ID: %s", config->client_id);
        ESP_LOGI(TAG, "  Default topic: %s", config->topic);
    }
    
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
    
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s:%d", 
             current_config.broker_host, current_config.broker_port);
    
    module_status.connection_status = MQTT_STATUS_CONNECTING;
    
    char connect_cmd[512];  // Increased buffer size for safety
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
    
    if (send_mqtt_at_command(connect_cmd, "OK", 15000)) {
        module_status.connection_status = MQTT_STATUS_CONNECTED;
        module_status.stats.connection_count++;
        module_status.stats.uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        ESP_LOGI(TAG, "Connected to MQTT broker successfully");
        return true;
    } else {
        module_status.connection_status = MQTT_STATUS_ERROR;
        strncpy(module_status.last_error_message, "Connection failed", 
                sizeof(module_status.last_error_message) - 1);
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return false;
    }
}

static bool mqtt_disconnect_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    if (module_status.connection_status != MQTT_STATUS_CONNECTED) {
        return true;
    }
    
    if (send_mqtt_at_command("AT+CMQTTDISC=0,60", "OK", 10000)) {
        module_status.connection_status = MQTT_STATUS_DISCONNECTED;
        ESP_LOGI(TAG, "Disconnected from MQTT broker");
        return true;
    }
    
    return false;
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
    if (!message || !result) {
        return false;
    }
    
    memset(result, 0, sizeof(mqtt_publish_result_t));
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (!mqtt_is_connected_impl()) {
        strncpy(result->error_message, "Not connected", sizeof(result->error_message) - 1);
        return false;
    }
    
    // Set topic
    if (!set_mqtt_topic(message->topic)) {
        strncpy(result->error_message, "Failed to set topic", sizeof(result->error_message) - 1);
        return false;
    }
    
    // Set payload
    if (!set_mqtt_payload(message->payload)) {
        strncpy(result->error_message, "Failed to set payload", sizeof(result->error_message) - 1);
        return false;
    }
    
    // Publish message
    if (publish_mqtt_message(result)) {
        result->success = true;
        result->publish_time_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_time;
        module_status.stats.messages_sent++;
        module_status.stats.bytes_sent += strlen(message->payload);
        module_status.last_publish_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (current_config.debug_output) {
            ESP_LOGI(TAG, "Published to topic '%s' (%d bytes)", 
                     message->topic, (int)strlen(message->payload));
        }
        return true;
    } else {
        module_status.stats.messages_failed++;
        return false;
    }
}

static bool mqtt_publish_json_impl(const char* topic, const char* json_payload, mqtt_publish_result_t* result)
{
    if (!topic || !json_payload || !result) {
        return false;
    }
    
    mqtt_message_t message;
    if (!mqtt_create_message(&message, topic, json_payload, current_config.qos_level, current_config.retain_messages)) {
        return false;
    }
    
    return mqtt_publish_impl(&message, result);
}

static bool mqtt_subscribe_impl(const char* topic, int qos)
{
    if (!topic) {
        return false;
    }
    
    // Set subscription topic
    char topic_cmd[128];
    snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTSUB=0,%d,%d", (int)strlen(topic), qos);
    
    if (send_mqtt_at_command(topic_cmd, ">", 2000)) {
        uart_write_bytes(UART_NUM_1, topic, strlen(topic));
        uart_write_bytes(UART_NUM_1, "\r\n", 2);
        
        if (send_mqtt_at_command("", "OK", 5000)) {
            ESP_LOGI(TAG, "Subscribed to topic: %s", topic);
            return true;
        }
    }
    
    ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
    return false;
}

static bool mqtt_unsubscribe_impl(const char* topic)
{
    if (!topic) {
        return false;
    }
    
    char unsub_cmd[128];
    snprintf(unsub_cmd, sizeof(unsub_cmd), "AT+CMQTTUNSUB=0,%d", (int)strlen(topic));
    
    if (send_mqtt_at_command(unsub_cmd, ">", 2000)) {
        uart_write_bytes(UART_NUM_1, topic, strlen(topic));
        uart_write_bytes(UART_NUM_1, "\r\n", 2);
        
        if (send_mqtt_at_command("", "OK", 5000)) {
            ESP_LOGI(TAG, "Unsubscribed from topic: %s", topic);
            return true;
        }
    }
    
    ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
    return false;
}

static bool mqtt_is_connected_impl(void)
{
    return module_status.connection_status == MQTT_STATUS_CONNECTED;
}

static void mqtt_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// Helper function implementations
static bool send_mqtt_at_command(const char* command, const char* expected, int timeout_ms)
{
    if (!command) {
        return false;
    }
    
    // Send command
    if (strlen(command) > 0) {
        char cmd_buffer[256];
        snprintf(cmd_buffer, sizeof(cmd_buffer), "%s\r\n", command);
        uart_write_bytes(UART_NUM_1, cmd_buffer, strlen(cmd_buffer));
        
        if (current_config.debug_output) {
            ESP_LOGI(TAG, "MQTT CMD: %s", command);
        }
    }
    
    if (!expected || strlen(expected) == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return true;
    }
    
    // Wait for response
    char* response_buffer = malloc(512);
    if (!response_buffer) {
        return false;
    }
    
    int total_len = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        int len = uart_read_bytes(UART_NUM_1, response_buffer + total_len, 
                                 511 - total_len, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
            response_buffer[total_len] = '\0';
            
            if (strstr(response_buffer, expected)) {
                if (current_config.debug_output) {
                    ESP_LOGI(TAG, "MQTT RSP: Found '%s'", expected);
                }
                free(response_buffer);
                return true;
            }
            
            if (strstr(response_buffer, "ERROR") || strstr(response_buffer, "+CME ERROR")) {
                if (current_config.debug_output) {
                    ESP_LOGW(TAG, "MQTT ERR: %s", response_buffer);
                }
                break;
            }
        }
    }
    
    if (current_config.debug_output) {
        ESP_LOGW(TAG, "MQTT TIMEOUT: Expected '%s', got '%s'", expected, response_buffer);
    }
    free(response_buffer);
    return false;
}

static bool set_mqtt_topic(const char* topic)
{
    if (!topic) {
        return false;
    }
    
    char topic_cmd[128];
    snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTTOPIC=0,%d", (int)strlen(topic));
    
    if (send_mqtt_at_command(topic_cmd, ">", 2000)) {
        uart_write_bytes(UART_NUM_1, topic, strlen(topic));
        uart_write_bytes(UART_NUM_1, "\r\n", 2);
        return send_mqtt_at_command("", "OK", 2000);
    }
    
    return false;
}

static bool set_mqtt_payload(const char* payload)
{
    if (!payload) {
        return false;
    }
    
    char payload_cmd[64];
    snprintf(payload_cmd, sizeof(payload_cmd), "AT+CMQTTPAYLOAD=0,%d", (int)strlen(payload));
    
    if (send_mqtt_at_command(payload_cmd, ">", 2000)) {
        uart_write_bytes(UART_NUM_1, payload, strlen(payload));
        uart_write_bytes(UART_NUM_1, "\r\n", 2);
        return send_mqtt_at_command("", "OK", 2000);
    }
    
    return false;
}

static bool publish_mqtt_message(mqtt_publish_result_t* result)
{
    if (send_mqtt_at_command("AT+CMQTTPUB=0,0,60", "+CMQTTPUB", 10000)) {
        result->message_id = module_status.stats.messages_sent + 1;
        return true;
    }
    
    strncpy(result->error_message, "Publish command failed", sizeof(result->error_message) - 1);
    return false;
}

// Utility functions
const char* mqtt_status_to_string(mqtt_status_t status)
{
    switch (status) {
        case MQTT_STATUS_DISCONNECTED: return "Disconnected";
        case MQTT_STATUS_CONNECTING: return "Connecting";
        case MQTT_STATUS_CONNECTED: return "Connected";
        case MQTT_STATUS_ERROR: return "Error";
        default: return "Unknown";
    }
}

bool mqtt_create_message(mqtt_message_t* msg, const char* topic, const char* payload, int qos, bool retain)
{
    if (!msg || !topic || !payload) {
        return false;
    }
    
    memset(msg, 0, sizeof(mqtt_message_t));
    
    strncpy(msg->topic, topic, sizeof(msg->topic) - 1);
    strncpy(msg->payload, payload, sizeof(msg->payload) - 1);
    msg->qos = qos;
    msg->retain = retain;
    msg->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    return true;
}

bool mqtt_validate_topic(const char* topic)
{
    if (!topic || strlen(topic) == 0 || strlen(topic) > 127) {
        return false;
    }
    
    // Check for invalid characters
    if (strchr(topic, '#') || strchr(topic, '+')) {
        return false; // Wildcards not allowed in publish topics
    }
    
    return true;
}

void mqtt_print_stats(const mqtt_stats_t* stats)
{
    if (!stats) {
        return;
    }
    
    ESP_LOGI(TAG, "MQTT Statistics:");
    ESP_LOGI(TAG, "  Messages sent: %lu", stats->messages_sent);
    ESP_LOGI(TAG, "  Messages failed: %lu", stats->messages_failed);
    ESP_LOGI(TAG, "  Bytes sent: %lu", stats->bytes_sent);
    ESP_LOGI(TAG, "  Connections: %lu", stats->connection_count);
    ESP_LOGI(TAG, "  Uptime: %lu ms", stats->uptime_ms);
}