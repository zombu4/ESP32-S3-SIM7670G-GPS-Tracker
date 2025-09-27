/**
 * 📡 MQTT PUBLISHER MODULE INTERFACE
 * 
 * Separate module for publishing GPS tracking data via MQTT
 * Handles message publishing, retry logic, and status reporting
 */

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "modules/gps/gps_nmea_parser.h"
#include "mqtt_connection_manager.h"
#include "mqtt_message_builder.h"

// 📡 PUBLISH PRIORITY LEVELS 📡

typedef enum {
    MQTT_PRIORITY_LOW = 0,      // System status, diagnostics
    MQTT_PRIORITY_NORMAL = 1,   // Regular GPS updates
    MQTT_PRIORITY_HIGH = 2,     // GPS fix acquired/lost
    MQTT_PRIORITY_CRITICAL = 3  // Battery critical, emergencies
} mqtt_publish_priority_t;

// 📡 PUBLISH RESULT 📡

typedef struct {
    bool success;
    int message_id;
    uint32_t publish_time;
    size_t message_size;
    char error_message[128];
    mqtt_publish_priority_t priority;
} mqtt_publish_result_t;

// 📡 PUBLISHER CONFIGURATION 📡

typedef struct {
    const char* device_id;
    
    // Publishing intervals (milliseconds)
    uint32_t gps_publish_interval;
    uint32_t battery_publish_interval;
    uint32_t system_publish_interval;
    
    // Retry settings
    uint8_t max_publish_retries;
    uint32_t retry_delay_ms;
    
    // QoS settings
    int gps_qos;
    int battery_qos;
    int system_qos;
    
    // Message options
    bool use_retained_messages;
    bool include_timestamp;
    bool validate_coordinates;
    bool debug_enabled;
    
} mqtt_publisher_config_t;

// 📡 PUBLISHER STATISTICS 📡

typedef struct {
    uint32_t gps_messages_published;
    uint32_t battery_messages_published;
    uint32_t system_messages_published;
    uint32_t total_publish_attempts;
    uint32_t publish_failures;
    uint32_t retry_attempts;
    
    uint32_t last_gps_publish_time;
    uint32_t last_battery_publish_time;
    uint32_t last_system_publish_time;
    
    size_t total_bytes_published;
    mqtt_publish_result_t last_publish_result;
    
} mqtt_publisher_stats_t;

// 📡 MQTT PUBLISHER INTERFACE 📡

typedef struct {
    /**
     * Initialize MQTT publisher with configuration
     * @param config Publisher configuration
     * @param conn_mgr Connection manager interface
     * @param msg_builder Message builder interface
     * @return true if initialization successful
     */
    bool (*initialize)(
        const mqtt_publisher_config_t* config,
        const mqtt_connection_manager_interface_t* conn_mgr,
        const mqtt_message_builder_interface_t* msg_builder
    );
    
    /**
     * Publish GPS tracking data
     * @param gps_data GPS NMEA data structure
     * @param priority Message priority
     * @param result Publish result (can be NULL)
     * @return true if publish initiated successfully
     */
    bool (*publish_gps_data)(
        const gps_nmea_data_t* gps_data,
        mqtt_publish_priority_t priority,
        mqtt_publish_result_t* result
    );
    
    /**
     * Publish battery status data
     * @param battery_data Battery data structure  
     * @param priority Message priority
     * @param result Publish result (can be NULL)
     * @return true if publish initiated successfully
     */
    bool (*publish_battery_data)(
        const mqtt_battery_data_t* battery_data,
        mqtt_publish_priority_t priority,
        mqtt_publish_result_t* result
    );
    
    /**
     * Publish system status data
     * @param system_data System data structure
     * @param priority Message priority
     * @param result Publish result (can be NULL)
     * @return true if publish initiated successfully
     */
    bool (*publish_system_data)(
        const mqtt_system_data_t* system_data,
        mqtt_publish_priority_t priority,
        mqtt_publish_result_t* result
    );
    
    /**
     * Publish combined tracking message with GPS, battery, and system data
     * @param gps_data GPS data (can be NULL to exclude)
     * @param battery_data Battery data (can be NULL to exclude)
     * @param system_data System data (can be NULL to exclude)
     * @param priority Message priority
     * @param result Publish result (can be NULL)
     * @return true if publish initiated successfully
     */
    bool (*publish_tracking_message)(
        const gps_nmea_data_t* gps_data,
        const mqtt_battery_data_t* battery_data,
        const mqtt_system_data_t* system_data,
        mqtt_publish_priority_t priority,
        mqtt_publish_result_t* result
    );
    
    /**
     * Check if publisher is ready to publish
     * @return true if connection manager is connected and ready
     */
    bool (*is_ready)(void);
    
    /**
     * Process pending publishes and handle retries
     * Call this periodically from main task
     */
    void (*process)(void);
    
    /**
     * Check if it's time to publish periodic data
     * @param data_type 0=GPS, 1=Battery, 2=System
     * @return true if publish interval has elapsed
     */
    bool (*should_publish_periodic)(uint8_t data_type);
    
    /**
     * Get publisher statistics
     * @param stats Pointer to stats structure to fill
     */
    void (*get_stats)(mqtt_publisher_stats_t* stats);
    
    /**
     * Get debug information string
     * @param debug_str Buffer for debug string
     * @param max_len Maximum buffer length
     */
    void (*get_debug_info)(char* debug_str, size_t max_len);
    
    /**
     * Reset publisher statistics
     */
    void (*reset_stats)(void);
    
    /**
     * Update configuration (some changes require restart to take effect)
     * @param config New configuration
     * @return true if configuration updated
     */
    bool (*update_config)(const mqtt_publisher_config_t* config);
    
} mqtt_publisher_interface_t;

// 📡 INTERFACE ACCESS 📡

/**
 * Get the MQTT publisher interface
 * @return Pointer to interface structure
 */
const mqtt_publisher_interface_t* mqtt_publisher_get_interface(void);

// 📡 DEFAULT CONFIGURATION 📡

/**
 * Get default MQTT publisher configuration
 * @return Default configuration structure
 */
mqtt_publisher_config_t mqtt_publisher_get_default_config(void);

#endif // MQTT_PUBLISHER_H