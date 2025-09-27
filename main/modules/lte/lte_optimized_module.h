#ifndef LTE_OPTIMIZED_MODULE_H
#define LTE_OPTIMIZED_MODULE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optimized LTE Module - Persistent Connection with Reduced Timeouts
 * 
 * This module addresses the timeout issues by:
 * 1. Establishing persistent connection that stays open
 * 2. Implementing connection pooling and reuse
 * 3. Reducing AT command timeouts with smarter retry logic
 * 4. Background connection monitoring and auto-recovery
 * 5. Batch operations to minimize command overhead
 */

// Connection States
typedef enum {
    LTE_OPT_STATE_DISCONNECTED = 0,
    LTE_OPT_STATE_INITIALIZING,
    LTE_OPT_STATE_CONNECTED,
    LTE_OPT_STATE_READY,
    LTE_OPT_STATE_ERROR
} lte_opt_state_t;

// Connection Pool Status
typedef struct {
    lte_opt_state_t state;
    bool persistent_connection_active;
    bool data_bearer_active;
    bool mqtt_session_active;
    char ip_address[16];
    int signal_strength;
    uint32_t connection_uptime_ms;
    uint32_t last_activity_ms;
    uint32_t successful_operations;
    uint32_t failed_operations;
} lte_opt_status_t;

// Optimization Configuration
typedef struct {
    char apn[64];
    char username[32];
    char password[32];
    
    // Connection Optimization
    bool persistent_connection;      // Keep connection always open
    uint32_t keepalive_interval_ms; // How often to send keepalive
    uint32_t reduced_timeout_ms;    // Reduced AT command timeout
    uint32_t fast_retry_count;      // Quick retries before fallback
    
    // UART Configuration
    int uart_tx_pin;
    int uart_rx_pin;
    int uart_baud_rate;
    
    // Monitoring
    bool enable_monitoring;         // Background monitoring task
    uint32_t health_check_interval_ms;
    bool auto_recovery;
    
    // Debug
    bool debug_enabled;
    
} lte_opt_config_t;

// Default Optimized Configuration
#define LTE_OPT_CONFIG_DEFAULT() { \
    .apn = "m2mglobal", \
    .username = "", \
    .password = "", \
    .persistent_connection = true, \
    .keepalive_interval_ms = 30000, \
    .reduced_timeout_ms = 3000, \
    .fast_retry_count = 3, \
    .uart_tx_pin = 18, \
    .uart_rx_pin = 17, \
    .uart_baud_rate = 115200, \
    .enable_monitoring = true, \
    .health_check_interval_ms = 60000, \
    .auto_recovery = true, \
    .debug_enabled = true \
}

// Event Callbacks
typedef void (*lte_opt_event_callback_t)(lte_opt_state_t state, void* user_data);

// Optimized Interface
typedef struct {
    // Lifecycle Management
    bool (*init)(const lte_opt_config_t* config);
    bool (*deinit)(void);
    
    // Connection Management (optimized)
    bool (*start_persistent_connection)(void);
    bool (*stop_connection)(void);
    bool (*is_connected)(void);
    
    // Fast Operations (using persistent connection)
    bool (*fast_mqtt_publish)(const char* topic, const char* data);
    bool (*fast_http_post)(const char* url, const char* data);
    bool (*fast_ping)(const char* host, uint32_t* response_time_ms);
    
    // Batch Operations (multiple operations in single transaction)
    bool (*batch_mqtt_start)(void);
    bool (*batch_mqtt_publish)(const char* topic, const char* data);
    bool (*batch_mqtt_end)(void);
    
    // Status and Monitoring
    lte_opt_state_t (*get_state)(void);
    bool (*get_status)(lte_opt_status_t* status);
    
    // Event Management
    bool (*register_event_callback)(lte_opt_event_callback_t callback, void* user_data);
    
    // Utility Functions
    bool (*test_connection)(void);
    void (*set_debug)(bool enable);
    
} lte_opt_interface_t;

/**
 * @brief Get the optimized LTE interface
 * @return Pointer to optimized LTE interface structure
 */
const lte_opt_interface_t* lte_opt_get_interface(void);

/**
 * @brief Initialize optimized LTE module with persistent connection
 * @param config Optimization configuration parameters
 * @return true if successful, false otherwise
 */
bool lte_opt_init(const lte_opt_config_t* config);

/**
 * @brief Start persistent LTE connection (reduces per-operation overhead)
 * @return true if connection established, false otherwise
 */
bool lte_opt_start_persistent_connection(void);

/**
 * @brief Check if optimized connection is active and ready
 * @return true if ready for fast operations, false otherwise
 */
bool lte_opt_is_ready(void);

/**
 * @brief Fast MQTT publish using persistent connection (no reconnection overhead)
 * @param topic MQTT topic to publish to
 * @param data Data to publish
 * @return true if publish successful, false otherwise
 */
bool lte_opt_fast_mqtt_publish(const char* topic, const char* data);

/**
 * @brief Test optimized connection performance
 * @return true if connection is performing well, false otherwise
 */
bool lte_opt_test_performance(void);

/**
 * @brief Get current optimized status
 * @param status Pointer to status structure to fill
 * @return true if status retrieved, false otherwise
 */
bool lte_opt_get_status(lte_opt_status_t* status);

/**
 * @brief Stop optimized connection and cleanup
 */
void lte_opt_stop(void);

#ifdef __cplusplus
}
#endif

#endif // LTE_OPTIMIZED_MODULE_H