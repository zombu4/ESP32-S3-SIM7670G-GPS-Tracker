#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../config.h"
#include "gps/gps_module.h"
#include "battery/battery_module.h"

// MQTT connection status
typedef enum {
 MQTT_STATUS_DISCONNECTED = 0,
 MQTT_STATUS_CONNECTING,
 MQTT_STATUS_CONNECTED,
 MQTT_STATUS_ERROR
} mqtt_status_t;

// MQTT message structure
typedef struct {
 char topic[128];
 char payload[512];
 int qos;
 bool retain;
 uint32_t timestamp;
} mqtt_message_t;

// MQTT statistics
typedef struct {
 uint32_t messages_sent;
 uint32_t messages_failed;
 uint32_t bytes_sent;
 uint32_t connection_count;
 uint32_t last_error_code;
 uint32_t uptime_ms;
} mqtt_stats_t;

// MQTT module status
typedef struct {
 bool initialized;
 mqtt_status_t connection_status;
 mqtt_stats_t stats;
 char last_error_message[128];
 uint32_t last_publish_time;
} mqtt_module_status_t;

// MQTT publish result
typedef struct {
 bool success;
 uint32_t message_id;
 uint32_t publish_time_ms;
 char error_message[64];
} mqtt_publish_result_t;

// MQTT module interface
typedef struct {
 bool (*init)(const mqtt_config_t* config);
 bool (*deinit)(void);
 bool (*connect)(void);
 bool (*disconnect)(void);
 mqtt_status_t (*get_status)(void);
 bool (*get_module_status)(mqtt_module_status_t* status);
 bool (*publish)(const mqtt_message_t* message, mqtt_publish_result_t* result);
 bool (*publish_json)(const char* topic, const char* json_payload, mqtt_publish_result_t* result);
 bool (*subscribe)(const char* topic, int qos);
 bool (*unsubscribe)(const char* topic);
 bool (*is_connected)(void);
 void (*set_debug)(bool enable);
} mqtt_interface_t;

// Get MQTT module interface
const mqtt_interface_t* mqtt_get_interface(void);

// MQTT utility functions
const char* mqtt_status_to_string(mqtt_status_t status);
bool mqtt_create_message(mqtt_message_t* msg, const char* topic, const char* payload, int qos, bool retain);
bool mqtt_validate_topic(const char* topic);
void mqtt_print_stats(const mqtt_stats_t* stats);

// Convenience functions
bool mqtt_publish_gps_data(const char* latitude, const char* longitude, 
 float battery_voltage, int battery_percentage);

// Enhanced JSON payload creation with GPS and battery data structures
bool mqtt_create_enhanced_json_payload(const gps_data_t* gps_data, const battery_data_t* battery_data,
 bool fresh_gps_data, char* json_buffer, size_t buffer_size);