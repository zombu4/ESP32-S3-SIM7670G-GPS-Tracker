#ifndef LTE_PPP_NATIVE_H
#define LTE_PPP_NATIVE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"

// Use the official ESP modem API
#include "esp_modem_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LTE PPP configuration structure
 */
typedef struct {
    // UART configuration
    int tx_pin;              /*!< UART TX pin */
    int rx_pin;              /*!< UART RX pin */
    int rts_pin;             /*!< UART RTS pin (-1 to disable) */
    int cts_pin;             /*!< UART CTS pin (-1 to disable) */
    int baud_rate;           /*!< UART baud rate */
    
    // Network configuration
    const char* apn;         /*!< Access Point Name */
    const char* username;    /*!< Username (can be NULL) */
    const char* password;    /*!< Password (can be NULL) */
    
    // Modem configuration
    int power_pin;           /*!< Power control pin (-1 to disable) */
    int reset_pin;           /*!< Reset pin (-1 to disable) */
    
    // PPP configuration
    bool auto_reconnect;     /*!< Auto reconnect on connection loss */
    int reconnect_timeout_s; /*!< Reconnect timeout in seconds */
    
} lte_ppp_config_t;

/**
 * @brief LTE connection events
 */
typedef enum {
    LTE_PPP_EVENT_UNKNOWN = 0,
    LTE_PPP_EVENT_CONNECTED,      /*!< PPP connection established */
    LTE_PPP_EVENT_DISCONNECTED,   /*!< PPP connection lost */
    LTE_PPP_EVENT_GOT_IP,         /*!< Got IP address */
    LTE_PPP_EVENT_LOST_IP,        /*!< Lost IP address */
    LTE_PPP_EVENT_RECONNECTING,   /*!< Attempting to reconnect */
} lte_ppp_event_t;

/**
 * @brief LTE connection state
 */
typedef enum {
    LTE_PPP_STATE_IDLE = 0,
    LTE_PPP_STATE_CONNECTING,
    LTE_PPP_STATE_CONNECTED,
    LTE_PPP_STATE_DISCONNECTING,
    LTE_PPP_STATE_ERROR,
} lte_ppp_state_t;

/**
 * @brief LTE PPP handle
 */
typedef struct lte_ppp_handle_s* lte_ppp_handle_t;

/**
 * @brief Event callback function type
 */
typedef void (*lte_ppp_event_cb_t)(lte_ppp_event_t event, void* event_data, void* user_data);

/**
 * @brief Initialize LTE PPP module
 * 
 * @param config Configuration structure
 * @param handle Output handle
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_init(const lte_ppp_config_t* config, lte_ppp_handle_t* handle);

/**
 * @brief Start LTE PPP connection
 * 
 * @param handle LTE PPP handle
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_start(lte_ppp_handle_t handle);

/**
 * @brief Stop LTE PPP connection
 * 
 * @param handle LTE PPP handle
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_stop(lte_ppp_handle_t handle);

/**
 * @brief Get connection state
 * 
 * @param handle LTE PPP handle
 * @return Current connection state
 */
lte_ppp_state_t lte_ppp_get_state(lte_ppp_handle_t handle);

/**
 * @brief Get network interface
 * 
 * @param handle LTE PPP handle
 * @return Network interface handle (NULL if not connected)
 */
esp_netif_t* lte_ppp_get_netif(lte_ppp_handle_t handle);

/**
 * @brief Register event callback
 * 
 * @param handle LTE PPP handle
 * @param callback Callback function
 * @param user_data User data for callback
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_register_event_cb(lte_ppp_handle_t handle, lte_ppp_event_cb_t callback, void* user_data);

/**
 * @brief Send AT command (for diagnostics)
 * 
 * @param handle LTE PPP handle
 * @param command AT command string
 * @param response Response buffer
 * @param response_size Response buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_send_at_command(lte_ppp_handle_t handle, const char* command, 
                                  char* response, size_t response_size, int timeout_ms);

/**
 * @brief Get signal quality
 * 
 * @param handle LTE PPP handle
 * @param rssi Output RSSI value
 * @param ber Output bit error rate
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_get_signal_quality(lte_ppp_handle_t handle, int* rssi, int* ber);

/**
 * @brief Deinitialize LTE PPP module
 * 
 * @param handle LTE PPP handle
 * @return ESP_OK on success
 */
esp_err_t lte_ppp_deinit(lte_ppp_handle_t handle);

/**
 * @brief Default configuration for SIM7670G
 */
#define LTE_PPP_DEFAULT_CONFIG() { \
    .tx_pin = 17, \
    .rx_pin = 18, \
    .rts_pin = -1, \
    .cts_pin = -1, \
    .baud_rate = 115200, \
    .apn = "m2mglobal", \
    .username = NULL, \
    .password = NULL, \
    .power_pin = -1, \
    .reset_pin = -1, \
    .auto_reconnect = true, \
    .reconnect_timeout_s = 30, \
}

#ifdef __cplusplus
}
#endif

#endif // LTE_PPP_NATIVE_H