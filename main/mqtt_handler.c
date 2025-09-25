#include "tracker.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "cJSON.h"

static const char *TAG = "MQTT";
static tracker_config_t current_config = {0};
static bool mqtt_initialized = false;
static bool mqtt_connected = false;

// Forward declaration - implemented in this file
static bool send_at_command(const char* command, const char* expected_response, int timeout_ms);

static bool send_at_command(const char* command, const char* expected_response, int timeout_ms)
{
    if (!command) return false;
    
    // Send command
    char cmd_buffer[256];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s\r\n", command);
    uart_write_bytes(UART_NUM_1, cmd_buffer, strlen(cmd_buffer));
    
    ESP_LOGI(TAG, "AT CMD: %s", command);
    
    if (!expected_response) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return true;
    }
    
    // Wait for response
    char* response_buffer = malloc(1024);
    if (!response_buffer) return false;
    
    int total_len = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        int len = uart_read_bytes(UART_NUM_1, response_buffer + total_len, 
                                 1023 - total_len, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
            response_buffer[total_len] = '\0';
            
            if (strstr(response_buffer, expected_response)) {
                ESP_LOGI(TAG, "AT RSP: Found '%s'", expected_response);
                free(response_buffer);
                return true;
            }
            
            // Also log any error responses
            if (strstr(response_buffer, "ERROR") || strstr(response_buffer, "+CME ERROR")) {
                ESP_LOGW(TAG, "AT ERR: %s", response_buffer);
                break;
            }
        }
    }
    
    ESP_LOGW(TAG, "AT TIMEOUT: Expected '%s', got '%s'", expected_response, response_buffer);
    free(response_buffer);
    return false;
}

bool mqtt_init(const tracker_config_t *config)
{
    if (!config) {
        return false;
    }
    
    memcpy(&current_config, config, sizeof(tracker_config_t));
    
    ESP_LOGI(TAG, "Initializing MQTT client...");
    
    // Start MQTT service
    if (!send_at_command("AT+CMQTTSTART", "OK", 5000)) {
        ESP_LOGE(TAG, "Failed to start MQTT service");
        return false;
    }
    
    // Acquire MQTT client
    char client_cmd[128];
    snprintf(client_cmd, sizeof(client_cmd), "AT+CMQTTACCQ=0,\"%s\",0", config->client_id);
    if (!send_at_command(client_cmd, "OK", 5000)) {
        ESP_LOGE(TAG, "Failed to acquire MQTT client");
        return false;
    }
    
    mqtt_initialized = true;
    ESP_LOGI(TAG, "MQTT client initialized");
    return true;
}

static bool mqtt_connect(void)
{
    if (!mqtt_initialized) {
        return false;
    }
    
    if (mqtt_connected) {
        return true; // Already connected
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s:%d", current_config.mqtt_broker, current_config.mqtt_port);
    
    char connect_cmd[256];
    snprintf(connect_cmd, sizeof(connect_cmd), 
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,1",
             current_config.mqtt_broker, current_config.mqtt_port);
    
    if (send_at_command(connect_cmd, "OK", 15000)) {
        mqtt_connected = true;
        ESP_LOGI(TAG, "Connected to MQTT broker");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to connect to MQTT broker");
        return false;
    }
}

static bool mqtt_disconnect(void)
{
    if (!mqtt_connected) {
        return true;
    }
    
    if (send_at_command("AT+CMQTTDISC=0,60", "OK", 10000)) {
        mqtt_connected = false;
        ESP_LOGI(TAG, "Disconnected from MQTT broker");
        return true;
    }
    
    return false;
}

bool mqtt_publish_data(const gps_data_t *gps, const battery_data_t *battery)
{
    if (!mqtt_initialized || !gps || !battery) {
        return false;
    }
    
    // Try to connect if not connected
    if (!mqtt_connect()) {
        return false;
    }
    
    // Create JSON payload
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return false;
    }
    
    // Add GPS data
    if (gps->fix_valid) {
        cJSON *gps_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(gps_obj, "latitude", gps->latitude);
        cJSON_AddNumberToObject(gps_obj, "longitude", gps->longitude);
        cJSON_AddNumberToObject(gps_obj, "altitude", gps->altitude);
        cJSON_AddNumberToObject(gps_obj, "speed_kmh", gps->speed_kmh);
        cJSON_AddNumberToObject(gps_obj, "course", gps->course);
        cJSON_AddNumberToObject(gps_obj, "satellites", gps->satellites);
        if (strlen(gps->timestamp) > 0) {
            cJSON_AddStringToObject(gps_obj, "timestamp", gps->timestamp);
        }
        cJSON_AddItemToObject(json, "gps", gps_obj);
    } else {
        cJSON_AddStringToObject(json, "gps", "no_fix");
    }
    
    // Add battery data
    cJSON *battery_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(battery_obj, "percentage", battery->percentage);
    cJSON_AddNumberToObject(battery_obj, "voltage", battery->voltage);
    cJSON_AddBoolToObject(battery_obj, "charging", battery->charging);
    cJSON_AddItemToObject(json, "battery", battery_obj);
    
    // Add system info
    cJSON_AddNumberToObject(json, "uptime_ms", xTaskGetTickCount() * portTICK_PERIOD_MS);
    cJSON_AddStringToObject(json, "device_id", current_config.client_id);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return false;
    }
    
    bool result = false;
    
    // Set topic
    char topic_cmd[128];
    snprintf(topic_cmd, sizeof(topic_cmd), "AT+CMQTTTOPIC=0,%d", (int)strlen(current_config.mqtt_topic));
    if (send_at_command(topic_cmd, ">", 2000)) {
        uart_write_bytes(UART_NUM_1, current_config.mqtt_topic, strlen(current_config.mqtt_topic));
        uart_write_bytes(UART_NUM_1, "\r\n", 2);
        
        if (send_at_command("", "OK", 2000)) {
            // Set payload
            char payload_cmd[64];
            snprintf(payload_cmd, sizeof(payload_cmd), "AT+CMQTTPAYLOAD=0,%d", (int)strlen(json_string));
            
            if (send_at_command(payload_cmd, ">", 2000)) {
                uart_write_bytes(UART_NUM_1, json_string, strlen(json_string));
                uart_write_bytes(UART_NUM_1, "\r\n", 2);
                
                if (send_at_command("", "OK", 2000)) {
                    // Publish message
                    if (send_at_command("AT+CMQTTPUB=0,0,60", "+CMQTTPUB", 10000)) {
                        ESP_LOGI(TAG, "MQTT message published successfully");
                        if (current_config.debug_output) {
                            ESP_LOGI(TAG, "Payload: %s", json_string);
                        }
                        result = true;
                    }
                }
            }
        }
    }
    
    if (!result) {
        ESP_LOGE(TAG, "Failed to publish MQTT message");
        // Try to reconnect on next attempt
        mqtt_connected = false;
    }
    
    free(json_string);
    return result;
}

void tracker_set_config(const tracker_config_t *config)
{
    if (config) {
        memcpy(&current_config, config, sizeof(tracker_config_t));
        ESP_LOGI(TAG, "Configuration updated");
    }
}