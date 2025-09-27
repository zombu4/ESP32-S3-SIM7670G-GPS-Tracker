#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Connection States
typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTING,
    CONN_STATE_CONNECTED,
    CONN_STATE_ERROR,
    CONN_STATE_RECOVERING
} connection_state_t;

// Connection Status Structure
typedef struct {
    connection_state_t cellular_state;
    connection_state_t gps_state;
    connection_state_t mqtt_state;
    
    // Cellular details
    bool sim_ready;
    bool network_registered;
    bool pdp_active;
    char ip_address[16];
    int signal_strength;
    
    // GPS details  
    bool gps_powered;
    bool gps_fix;
    int satellites_visible;
    int satellites_used;
    
    // MQTT details
    bool mqtt_connected;
    uint32_t last_publish_time;
    
    // Health monitoring
    uint32_t cellular_uptime;
    uint32_t gps_uptime;
    uint32_t mqtt_uptime;
    uint32_t last_check_time;
} connection_status_t;

// Connection Recovery Configuration
typedef struct {
    uint32_t cellular_check_interval_ms;    // How often to check cellular
    uint32_t gps_check_interval_ms;         // How often to check GPS
    uint32_t mqtt_check_interval_ms;        // How often to check MQTT
    
    uint32_t cellular_timeout_ms;           // Max time to wait for cellular
    uint32_t gps_timeout_ms;                // Max time to wait for GPS fix
    uint32_t mqtt_timeout_ms;               // Max time to wait for MQTT
    
    uint8_t max_cellular_retries;           // Max cellular reconnection attempts
    uint8_t max_gps_retries;                // Max GPS restart attempts  
    uint8_t max_mqtt_retries;               // Max MQTT reconnection attempts
    
    bool auto_recovery_enabled;             // Enable automatic recovery
    bool debug_enabled;                     // Enable debug logging
} recovery_config_t;

// Connection Manager Interface
typedef struct connection_manager_interface {
    // Initialization
    bool (*init)(const recovery_config_t* config);
    bool (*deinit)(void);
    
    // Sequential startup (blocking until complete or timeout)
    bool (*startup_cellular)(uint32_t timeout_ms);
    bool (*startup_gps)(uint32_t timeout_ms);  
    bool (*startup_mqtt)(uint32_t timeout_ms);
    bool (*startup_full_system)(void);         // Complete sequential startup
    
    // Connection monitoring
    bool (*check_all_connections)(connection_status_t* status);
    bool (*is_cellular_healthy)(void);
    bool (*is_gps_healthy)(void);
    bool (*is_mqtt_healthy)(void);
    bool (*is_system_ready)(void);            // All connections healthy
    
    // Recovery operations
    bool (*recover_cellular)(void);
    bool (*recover_gps)(void);
    bool (*recover_mqtt)(void);
    bool (*recover_all)(void);
    
    // Status and control
    bool (*get_status)(connection_status_t* status);
    void (*start_monitoring)(void);            // Start background monitoring task
    void (*stop_monitoring)(void);             // Stop background monitoring
    void (*set_debug)(bool enable);
} connection_manager_interface_t;

// Get Connection Manager Interface
const connection_manager_interface_t* connection_manager_get_interface(void);

// Default Recovery Configuration
extern const recovery_config_t RECOVERY_CONFIG_DEFAULT;

// Connection state string helpers
const char* connection_state_to_string(connection_state_t state);

#ifdef __cplusplus
}
#endif

#endif // CONNECTION_MANAGER_H