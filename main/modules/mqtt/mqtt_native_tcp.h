#ifndef MQTT_NATIVE_TCP_H
#define MQTT_NATIVE_TCP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Native TCP MQTT configuration
 */
typedef struct {
    // Broker configuration
    const char* broker_uri;      /*!< MQTT broker URI (mqtt://host:port) */
    const char* client_id;       /*!< Client ID (auto-generated if NULL) */
    const char* username;        /*!< Username (can be NULL) */
    const char* password;        /*!< Password (can be NULL) */
    
    // Connection settings
    int port;                    /*!< Broker port (1883 for plain, 8883 for TLS) */
    int keepalive;               /*!< Keepalive interval in seconds */
    bool clean_session;          /*!< Clean session flag */
    
    // TLS settings
    bool use_tls;                /*!< Enable TLS/SSL */
    const char* cert_pem;        /*!< CA certificate (NULL for no verification) */
    
    // Quality of Service
    int qos;                     /*!< Default QoS level (0, 1, or 2) */
    bool retain;                 /*!< Default retain flag */
    
    // Timeouts
    int network_timeout_ms;      /*!< Network timeout */
    int reconnect_timeout_ms;    /*!< Reconnect timeout */
    
} mqtt_native_config_t;

/**
 * @brief MQTT connection events
 */
typedef enum {
    MQTT_NATIVE_EVENT_UNKNOWN = 0,
    MQTT_NATIVE_EVENT_CONNECTED,      /*!< Connected to broker */
    MQTT_NATIVE_EVENT_DISCONNECTED,   /*!< Disconnected from broker */
    MQTT_NATIVE_EVENT_SUBSCRIBED,     /*!< Successfully subscribed */
    MQTT_NATIVE_EVENT_UNSUBSCRIBED,   /*!< Successfully unsubscribed */
    MQTT_NATIVE_EVENT_PUBLISHED,      /*!< Message published successfully */
    MQTT_NATIVE_EVENT_DATA,           /*!< Data received */
    MQTT_NATIVE_EVENT_ERROR,          /*!< Error occurred */
} mqtt_native_event_t;

/**
 * @brief MQTT connection state
 */
typedef enum {
    MQTT_NATIVE_STATE_DISCONNECTED = 0,
    MQTT_NATIVE_STATE_CONNECTING,
    MQTT_NATIVE_STATE_CONNECTED,
    MQTT_NATIVE_STATE_ERROR,
} mqtt_native_state_t;

/**
 * @brief MQTT data structure for events
 */
typedef struct {
    char* topic;                 /*!< Topic name */
    char* data;                  /*!< Message data */
    int data_len;                /*!< Data length */
    int msg_id;                  /*!< Message ID */
    int qos;                     /*!< QoS level */
    bool retain;                 /*!< Retain flag */
} mqtt_native_data_t;

/**
 * @brief MQTT handle
 */
typedef struct mqtt_native_handle_s* mqtt_native_handle_t;

/**
 * @brief Event callback function type
 */
typedef void (*mqtt_native_event_cb_t)(mqtt_native_event_t event, mqtt_native_data_t* data, void* user_data);

/**
 * @brief Initialize native MQTT module
 * 
 * @param config Configuration structure
 * @param handle Output handle
 * @return ESP_OK on success
 */
esp_err_t mqtt_native_init(const mqtt_native_config_t* config, mqtt_native_handle_t* handle);

/**
 * @brief Connect to MQTT broker
 * 
 * @param handle MQTT handle
 * @return ESP_OK on success
 */
esp_err_t mqtt_native_connect(mqtt_native_handle_t handle);

/**
 * @brief Disconnect from MQTT broker
 * 
 * @param handle MQTT handle
 * @return ESP_OK on success
 */
esp_err_t mqtt_native_disconnect(mqtt_native_handle_t handle);

/**
 * @brief Get connection state
 * 
 * @param handle MQTT handle
 * @return Current connection state
 */
mqtt_native_state_t mqtt_native_get_state(mqtt_native_handle_t handle);

/**
 * @brief Publish message
 * 
 * @param handle MQTT handle
 * @param topic Topic to publish to
 * @param data Message data
 * @param data_len Data length (0 for strlen)
 * @param qos QoS level (-1 for default)
 * @param retain Retain flag (-1 for default)
 * @return Message ID on success, -1 on error
 */
int mqtt_native_publish(mqtt_native_handle_t handle, const char* topic, 
                       const char* data, int data_len, int qos, int retain);

/**
 * @brief Subscribe to topic
 * 
 * @param handle MQTT handle
 * @param topic Topic to subscribe to
 * @param qos QoS level (-1 for default)
 * @return Message ID on success, -1 on error
 */
int mqtt_native_subscribe(mqtt_native_handle_t handle, const char* topic, int qos);

/**
 * @brief Unsubscribe from topic
 * 
 * @param handle MQTT handle
 * @param topic Topic to unsubscribe from
 * @return Message ID on success, -1 on error
 */
int mqtt_native_unsubscribe(mqtt_native_handle_t handle, const char* topic);

/**
 * @brief Register event callback
 * 
 * @param handle MQTT handle
 * @param callback Callback function
 * @param user_data User data for callback
 * @return ESP_OK on success
 */
esp_err_t mqtt_native_register_event_cb(mqtt_native_handle_t handle, mqtt_native_event_cb_t callback, void* user_data);

/**
 * @brief Check if connected
 * 
 * @param handle MQTT handle
 * @return true if connected
 */
bool mqtt_native_is_connected(mqtt_native_handle_t handle);

/**
 * @brief Get last error
 * 
 * @param handle MQTT handle
 * @return Last error code
 */
esp_err_t mqtt_native_get_last_error(mqtt_native_handle_t handle);

/**
 * @brief Deinitialize MQTT module
 * 
 * @param handle MQTT handle
 * @return ESP_OK on success
 */
esp_err_t mqtt_native_deinit(mqtt_native_handle_t handle);

/**
 * @brief Default configuration
 */
#define MQTT_NATIVE_DEFAULT_CONFIG() { \
    .broker_uri = "mqtt://65.124.194.3:1883", \
    .client_id = NULL, \
    .username = NULL, \
    .password = NULL, \
    .port = 1883, \
    .keepalive = 60, \
    .clean_session = true, \
    .use_tls = false, \
    .cert_pem = NULL, \
    .qos = 0, \
    .retain = false, \
    .network_timeout_ms = 10000, \
    .reconnect_timeout_ms = 10000, \
}

#ifdef __cplusplus
}
#endif

#endif // MQTT_NATIVE_TCP_H