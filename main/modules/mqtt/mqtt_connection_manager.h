/**
 * 📡 MQTT CONNECTION MANAGER MODULE INTERFACE
 * 
 * Separate module for MQTT connection management and debugging
 * Handles connection establishment, status monitoring, and reconnection
 */

#ifndef MQTT_CONNECTION_MANAGER_H
#define MQTT_CONNECTION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 📡 MQTT CONNECTION STATUS 📡

typedef enum {
    MQTT_STATUS_DISCONNECTED = 0,
    MQTT_STATUS_CONNECTING,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_PUBLISHING,
    MQTT_STATUS_ERROR,
    MQTT_STATUS_RECONNECTING
} mqtt_connection_status_t;

// 📡 MQTT CONNECTION CONFIGURATION 📡

typedef struct {
    const char* broker_host;
    uint16_t broker_port;
    const char* client_id;
    const char* username;
    const char* password;
    
    // Connection timeouts and retry settings
    uint32_t connect_timeout_ms;
    uint32_t keepalive_seconds;
    uint32_t retry_interval_ms;
    uint8_t max_retry_attempts;
    
    // Debugging
    bool debug_enabled;
    
} mqtt_connection_config_t;

// 📡 MQTT CONNECTION STATISTICS 📡

typedef struct {
    uint32_t connection_attempts;
    uint32_t successful_connections;
    uint32_t connection_failures;
    uint32_t disconnection_count;
    uint32_t messages_published;
    uint32_t publish_failures;
    uint32_t last_connection_time;
    uint32_t uptime_seconds;
    
    // Current status
    mqtt_connection_status_t status;
    int last_error_code;
    char last_error_message[128];
    
} mqtt_connection_stats_t;

// 📡 MQTT CONNECTION MANAGER INTERFACE 📡

typedef struct {
    /**
     * Initialize MQTT connection manager with configuration
     * @param config Connection configuration
     * @return true if initialization successful
     */
    bool (*initialize)(const mqtt_connection_config_t* config);
    
    /**
     * Start MQTT connection process
     * @return true if connection initiated successfully
     */
    bool (*connect)(void);
    
    /**
     * Disconnect from MQTT broker
     * @return true if disconnect successful
     */
    bool (*disconnect)(void);
    
    /**
     * Check if MQTT is currently connected
     * @return true if connected and ready
     */
    bool (*is_connected)(void);
    
    /**
     * Get current connection status
     * @return Current status enum
     */
    mqtt_connection_status_t (*get_status)(void);
    
    /**
     * Handle connection events and maintenance
     * Call this periodically from main task
     */
    void (*process)(void);
    
    /**
     * Force reconnection attempt
     * @return true if reconnection initiated
     */
    bool (*reconnect)(void);
    
    /**
     * Get connection statistics for debugging
     * @param stats Pointer to stats structure to fill
     */
    void (*get_stats)(mqtt_connection_stats_t* stats);
    
    /**
     * Get debug information string
     * @param debug_str Buffer for debug string
     * @param max_len Maximum buffer length
     */
    void (*get_debug_info)(char* debug_str, size_t max_len);
    
    /**
     * Reset connection statistics
     */
    void (*reset_stats)(void);
    
    /**
     * Update configuration (requires reconnect to take effect)
     * @param config New configuration
     * @return true if configuration updated
     */
    bool (*update_config)(const mqtt_connection_config_t* config);
    
} mqtt_connection_manager_interface_t;

// 📡 INTERFACE ACCESS 📡

/**
 * Get the MQTT connection manager interface
 * @return Pointer to interface structure
 */
const mqtt_connection_manager_interface_t* mqtt_connection_manager_get_interface(void);

// 📡 DEFAULT CONFIGURATION 📡

/**
 * Get default MQTT connection configuration
 * @return Default configuration structure
 */
mqtt_connection_config_t mqtt_connection_manager_get_default_config(void);

#endif // MQTT_CONNECTION_MANAGER_H