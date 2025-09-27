#ifndef CELLULAR_NATIVE_STACK_H
#define CELLULAR_NATIVE_STACK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "lte_ppp_native.h"
#include "mqtt_native_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cellular native stack configuration
 */
typedef struct {
    // LTE configuration
    lte_ppp_config_t lte_config;
    
    // MQTT configuration  
    mqtt_native_config_t mqtt_config;
    
    // Integration settings
    bool auto_connect_mqtt;      /*!< Auto-connect MQTT when LTE is ready */
    int connection_check_ms;     /*!< Connection monitoring interval */
    
} cellular_native_config_t;

/**
 * @brief Stack events
 */
typedef enum {
    CELLULAR_NATIVE_EVENT_LTE_CONNECTED,
    CELLULAR_NATIVE_EVENT_LTE_DISCONNECTED,
    CELLULAR_NATIVE_EVENT_MQTT_CONNECTED,
    CELLULAR_NATIVE_EVENT_MQTT_DISCONNECTED,
    CELLULAR_NATIVE_EVENT_MQTT_DATA,
    CELLULAR_NATIVE_EVENT_STACK_READY,       /*!< Both LTE and MQTT ready */
    CELLULAR_NATIVE_EVENT_ERROR,
} cellular_native_event_t;

/**
 * @brief Stack handle
 */
typedef struct cellular_native_handle_s* cellular_native_handle_t;

/**
 * @brief Event callback function type
 */
typedef void (*cellular_native_event_cb_t)(cellular_native_event_t event, void* event_data, void* user_data);

/**
 * @brief Initialize cellular native stack
 * 
 * @param config Configuration structure
 * @param handle Output handle
 * @return ESP_OK on success
 */
esp_err_t cellular_native_init(const cellular_native_config_t* config, cellular_native_handle_t* handle);

/**
 * @brief Start cellular stack (LTE + optional MQTT)
 * 
 * @param handle Stack handle
 * @return ESP_OK on success
 */
esp_err_t cellular_native_start(cellular_native_handle_t handle);

/**
 * @brief Stop cellular stack
 * 
 * @param handle Stack handle
 * @return ESP_OK on success
 */
esp_err_t cellular_native_stop(cellular_native_handle_t handle);

/**
 * @brief Get LTE handle
 * 
 * @param handle Stack handle
 * @return LTE handle
 */
lte_ppp_handle_t cellular_native_get_lte_handle(cellular_native_handle_t handle);

/**
 * @brief Get MQTT handle
 * 
 * @param handle Stack handle
 * @return MQTT handle
 */
mqtt_native_handle_t cellular_native_get_mqtt_handle(cellular_native_handle_t handle);

/**
 * @brief Publish GPS data via MQTT
 * 
 * @param handle Stack handle
 * @param latitude Latitude
 * @param longitude Longitude
 * @param altitude Altitude
 * @param battery_voltage Battery voltage
 * @return Message ID on success, -1 on error
 */
int cellular_native_publish_gps_data(cellular_native_handle_t handle, 
                                    double latitude, double longitude, double altitude, 
                                    float battery_voltage);

/**
 * @brief Check if stack is fully ready (LTE + MQTT connected)
 * 
 * @param handle Stack handle
 * @return true if ready
 */
bool cellular_native_is_ready(cellular_native_handle_t handle);

/**
 * @brief Register event callback
 * 
 * @param handle Stack handle
 * @param callback Callback function
 * @param user_data User data
 * @return ESP_OK on success
 */
esp_err_t cellular_native_register_event_cb(cellular_native_handle_t handle, 
                                           cellular_native_event_cb_t callback, void* user_data);

/**
 * @brief Deinitialize cellular stack
 * 
 * @param handle Stack handle
 * @return ESP_OK on success
 */
esp_err_t cellular_native_deinit(cellular_native_handle_t handle);

/**
 * @brief Default configuration
 */
#define CELLULAR_NATIVE_DEFAULT_CONFIG() { \
    .lte_config = LTE_PPP_DEFAULT_CONFIG(), \
    .mqtt_config = MQTT_NATIVE_DEFAULT_CONFIG(), \
    .auto_connect_mqtt = true, \
    .connection_check_ms = 30000, \
}

#ifdef __cplusplus
}
#endif

#endif // CELLULAR_NATIVE_STACK_H