/**
 * 📨 MQTT MESSAGE BUILDER MODULE INTERFACE
 * 
 * Separate module for building MQTT messages and payloads
 * Handles JSON formatting, validation, and message construction
 */

#ifndef MQTT_MESSAGE_BUILDER_H
#define MQTT_MESSAGE_BUILDER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 📨 GPS DATA FOR MQTT MESSAGE 📨

typedef struct {
    bool has_valid_fix;
    double latitude;
    double longitude;
    float altitude;
    uint8_t satellites_used;
    uint8_t fix_quality;
    float hdop;
    uint32_t timestamp;
} mqtt_gps_data_t;

// 📨 BATTERY DATA FOR MQTT MESSAGE 📨

typedef struct {
    float voltage;
    float percentage;
    bool is_charging;
    float charge_rate;
    bool is_critical;
    uint32_t timestamp;
} mqtt_battery_data_t;

// 📨 SYSTEM DATA FOR MQTT MESSAGE 📨

typedef struct {
    uint32_t uptime_seconds;
    uint32_t free_heap;
    int8_t rssi;
    const char* firmware_version;
    const char* device_id;
    uint32_t timestamp;
} mqtt_system_data_t;

// 📨 MESSAGE BUILD OPTIONS 📨

typedef struct {
    bool include_gps_data;
    bool include_battery_data;
    bool include_system_data;
    bool pretty_format;
    bool include_timestamp;
    bool validate_coordinates;
    size_t max_message_size;
} mqtt_message_options_t;

// 📨 MESSAGE BUILD RESULT 📨

typedef struct {
    bool success;
    size_t message_length;
    char error_message[128];
    
    // Message components included
    bool gps_included;
    bool battery_included;
    bool system_included;
    
} mqtt_message_result_t;

// 📨 MQTT MESSAGE BUILDER INTERFACE 📨

typedef struct {
    /**
     * Build complete GPS tracking message in JSON format
     * @param gps_data GPS data to include (can be NULL to skip)
     * @param battery_data Battery data to include (can be NULL to skip)
     * @param system_data System data to include (can be NULL to skip)
     * @param options Message build options
     * @param message_buffer Buffer to write message to
     * @param buffer_size Size of message buffer
     * @param result Build result information
     * @return true if message built successfully
     */
    bool (*build_tracking_message)(
        const mqtt_gps_data_t* gps_data,
        const mqtt_battery_data_t* battery_data,
        const mqtt_system_data_t* system_data,
        const mqtt_message_options_t* options,
        char* message_buffer,
        size_t buffer_size,
        mqtt_message_result_t* result
    );
    
    /**
     * Build GPS-only message (minimal payload)
     * @param gps_data GPS data
     * @param message_buffer Buffer for message
     * @param buffer_size Buffer size
     * @param result Build result
     * @return true if successful
     */
    bool (*build_gps_message)(
        const mqtt_gps_data_t* gps_data,
        char* message_buffer,
        size_t buffer_size,
        mqtt_message_result_t* result
    );
    
    /**
     * Build battery status message
     * @param battery_data Battery data
     * @param message_buffer Buffer for message
     * @param buffer_size Buffer size
     * @param result Build result
     * @return true if successful
     */
    bool (*build_battery_message)(
        const mqtt_battery_data_t* battery_data,
        char* message_buffer,
        size_t buffer_size,
        mqtt_message_result_t* result
    );
    
    /**
     * Build system status message
     * @param system_data System data
     * @param message_buffer Buffer for message
     * @param buffer_size Buffer size
     * @param result Build result
     * @return true if successful
     */
    bool (*build_system_message)(
        const mqtt_system_data_t* system_data,
        char* message_buffer,
        size_t buffer_size,
        mqtt_message_result_t* result
    );
    
    /**
     * Validate GPS coordinates
     * @param latitude Latitude to validate
     * @param longitude Longitude to validate
     * @return true if coordinates are valid
     */
    bool (*validate_gps_coordinates)(double latitude, double longitude);
    
    /**
     * Get message size estimate for planning
     * @param options Message options
     * @return Estimated message size in bytes
     */
    size_t (*estimate_message_size)(const mqtt_message_options_t* options);
    
    /**
     * Get current UTC timestamp string
     * @param timestamp_buffer Buffer for timestamp
     * @param buffer_size Buffer size
     * @return true if timestamp generated
     */
    bool (*get_timestamp_string)(char* timestamp_buffer, size_t buffer_size);
    
    /**
     * Escape JSON string (if needed for custom fields)
     * @param input Input string
     * @param output Output buffer
     * @param output_size Output buffer size
     * @return true if successful
     */
    bool (*escape_json_string)(const char* input, char* output, size_t output_size);
    
    /**
     * Get debug information about last message build
     * @param debug_str Buffer for debug string
     * @param max_len Maximum buffer length
     */
    void (*get_debug_info)(char* debug_str, size_t max_len);
    
} mqtt_message_builder_interface_t;

// 📨 INTERFACE ACCESS 📨

/**
 * Get the MQTT message builder interface
 * @return Pointer to interface structure
 */
const mqtt_message_builder_interface_t* mqtt_message_builder_get_interface(void);

// 📨 DEFAULT OPTIONS 📨

/**
 * Get default message build options
 * @return Default options structure
 */
mqtt_message_options_t mqtt_message_builder_get_default_options(void);

// 📨 TOPIC HELPERS 📨

/**
 * Build topic string for GPS tracking data
 * @param device_id Device identifier
 * @param topic_buffer Buffer for topic string
 * @param buffer_size Buffer size
 * @return true if topic built successfully
 */
bool mqtt_message_builder_get_gps_topic(const char* device_id, char* topic_buffer, size_t buffer_size);

/**
 * Build topic string for battery data
 * @param device_id Device identifier
 * @param topic_buffer Buffer for topic string
 * @param buffer_size Buffer size
 * @return true if topic built successfully
 */
bool mqtt_message_builder_get_battery_topic(const char* device_id, char* topic_buffer, size_t buffer_size);

/**
 * Build topic string for system status
 * @param device_id Device identifier  
 * @param topic_buffer Buffer for topic string
 * @param buffer_size Buffer size
 * @return true if topic built successfully
 */
bool mqtt_message_builder_get_system_topic(const char* device_id, char* topic_buffer, size_t buffer_size);

#endif // MQTT_MESSAGE_BUILDER_H