#include "lte_ppp_native.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_netif_defaults.h"
#include "esp_event.h"
#include "esp_modem_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "LTE_PPP_NATIVE";

/**
 * @brief LTE PPP handle structure
 */
struct lte_ppp_handle_s {
    // ESP Modem objects
    esp_modem_dce_t* dce;
    esp_netif_t* esp_netif;
    
    // Configuration
    lte_ppp_config_t config;
    
    // State management
    lte_ppp_state_t state;
    SemaphoreHandle_t state_mutex;
    
    // Event handling
    lte_ppp_event_cb_t event_callback;
    void* user_data;
    
    // Connection monitoring
    TaskHandle_t monitor_task;
    bool monitor_running;
};

// Forward declarations
static void lte_ppp_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void lte_ppp_monitor_task(void* pvParameters);
static esp_err_t lte_ppp_configure_modem(lte_ppp_handle_t handle);

esp_err_t lte_ppp_init(const lte_ppp_config_t* config, lte_ppp_handle_t* handle)
{
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🚀 Initializing LTE PPP Native Module");
    ESP_LOGI(TAG, "📡 APN: %s", config->apn);
    ESP_LOGI(TAG, "📞 UART: TX=%d, RX=%d, Baud=%d", config->tx_pin, config->rx_pin, config->baud_rate);
    
    // Allocate handle
    lte_ppp_handle_t h = calloc(1, sizeof(struct lte_ppp_handle_s));
    if (!h) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for handle");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    memcpy(&h->config, config, sizeof(lte_ppp_config_t));
    h->state = LTE_PPP_STATE_IDLE;
    
    // Create state mutex
    h->state_mutex = xSemaphoreCreateMutex();
    if (!h->state_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create state mutex");
        free(h);
        return ESP_ERR_NO_MEM;
    }
    
    // Configure DTE (Data Terminal Equipment) - UART interface
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = config->tx_pin;
    dte_config.uart_config.rx_io_num = config->rx_pin;
    dte_config.uart_config.rts_io_num = config->rts_pin;
    dte_config.uart_config.cts_io_num = config->cts_pin;
    dte_config.uart_config.baud_rate = config->baud_rate;
    dte_config.uart_config.data_bits = UART_DATA_8_BITS;
    dte_config.uart_config.parity = UART_PARITY_DISABLE;
    dte_config.uart_config.stop_bits = UART_STOP_BITS_1;
    dte_config.uart_config.flow_control = (config->rts_pin >= 0 && config->cts_pin >= 0) ? 
                                          UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    
    // Configure DCE (Data Communication Equipment) - Modem interface
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(config->apn);
    // Note: Username/password are set via esp_netif_ppp_set_auth() after netif creation
    
    // Create PPP network interface
    const esp_netif_inherent_config_t ppp_base_cfg = {
        .flags = ESP_NETIF_DHCP_CLIENT | ESP_NETIF_FLAG_GARP | ESP_NETIF_FLAG_EVENT_IP_MODIFIED,
        .mac = { 0 },
        .ip_info = NULL,
        .get_ip_event = IP_EVENT_PPP_GOT_IP,
        .lost_ip_event = IP_EVENT_PPP_LOST_IP,
        .if_key = "PPP_DEF",
        .if_desc = "ppp",
        .route_prio = 32
    };
    
    // Use the ESP_NETIF_DEFAULT_PPP() macro directly
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    
    h->esp_netif = esp_netif_new(&netif_ppp_config);
    
    // Set PPP authentication if username/password provided  
    if (config->username && config->password) {
        esp_netif_ppp_set_auth(h->esp_netif, NETIF_PPP_AUTHTYPE_PAP, config->username, config->password);
    }
    if (!h->esp_netif) {
        ESP_LOGE(TAG, "❌ Failed to create network interface");
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return ESP_FAIL;
    }
    
    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, 
                                              &lte_ppp_event_handler, h));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, 
                                              &lte_ppp_event_handler, h));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, 
                                              &lte_ppp_event_handler, h));
    
    // Create DCE object for SIM7600 (compatible with SIM7670)
    h->dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, h->esp_netif);
    if (!h->dce) {
        ESP_LOGE(TAG, "❌ Failed to create DCE object");
        esp_netif_destroy(h->esp_netif);
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ LTE PPP Native Module initialized successfully");
    *handle = h;
    
    return ESP_OK;
}

esp_err_t lte_ppp_start(lte_ppp_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Starting LTE PPP connection...");
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    
    if (handle->state != LTE_PPP_STATE_IDLE) {
        ESP_LOGW(TAG, "⚠️ PPP already starting/started (state: %d)", handle->state);
        xSemaphoreGive(handle->state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    handle->state = LTE_PPP_STATE_CONNECTING;
    xSemaphoreGive(handle->state_mutex);
    
    // Configure modem for data mode
    esp_err_t err = lte_ppp_configure_modem(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to configure modem: %s", esp_err_to_name(err));
        handle->state = LTE_PPP_STATE_ERROR;
        return err;
    }
    
    // Switch to PPP mode (data mode)
    err = esp_modem_set_mode(handle->dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to switch to data mode: %s", esp_err_to_name(err));
        handle->state = LTE_PPP_STATE_ERROR;
        return err;
    }
    
    // Start connection monitoring task
    handle->monitor_running = true;
    xTaskCreate(lte_ppp_monitor_task, "lte_ppp_monitor", 4096, handle, 5, &handle->monitor_task);
    
    ESP_LOGI(TAG, "🎯 PPP connection initiated");
    
    return ESP_OK;
}

esp_err_t lte_ppp_stop(lte_ppp_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Stopping LTE PPP connection...");
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    handle->state = LTE_PPP_STATE_DISCONNECTING;
    xSemaphoreGive(handle->state_mutex);
    
    // Stop monitoring task
    if (handle->monitor_task) {
        handle->monitor_running = false;
        vTaskDelete(handle->monitor_task);
        handle->monitor_task = NULL;
    }
    
    // Switch back to command mode
    esp_err_t err = esp_modem_set_mode(handle->dce, ESP_MODEM_MODE_COMMAND);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to switch to command mode: %s", esp_err_to_name(err));
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    handle->state = LTE_PPP_STATE_IDLE;
    xSemaphoreGive(handle->state_mutex);
    
    ESP_LOGI(TAG, "✅ PPP connection stopped");
    
    return ESP_OK;
}

lte_ppp_state_t lte_ppp_get_state(lte_ppp_handle_t handle)
{
    if (!handle) {
        return LTE_PPP_STATE_ERROR;
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    lte_ppp_state_t state = handle->state;
    xSemaphoreGive(handle->state_mutex);
    
    return state;
}

esp_netif_t* lte_ppp_get_netif(lte_ppp_handle_t handle)
{
    if (!handle || handle->state != LTE_PPP_STATE_CONNECTED) {
        return NULL;
    }
    
    return handle->esp_netif;
}

esp_err_t lte_ppp_register_event_cb(lte_ppp_handle_t handle, lte_ppp_event_cb_t callback, void* user_data)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->event_callback = callback;
    handle->user_data = user_data;
    
    return ESP_OK;
}

esp_err_t lte_ppp_send_at_command(lte_ppp_handle_t handle, const char* command, 
                                  char* response, size_t response_size, int timeout_ms)
{
    if (!handle || !command) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Switch to command mode temporarily
    esp_err_t err = esp_modem_set_mode(handle->dce, ESP_MODEM_MODE_COMMAND);
    if (err != ESP_OK) {
        return err;
    }
    
    // Send command
    err = esp_modem_at(handle->dce, command, response, timeout_ms);
    
    // Switch back to data mode if we were connected
    if (handle->state == LTE_PPP_STATE_CONNECTED) {
        esp_modem_set_mode(handle->dce, ESP_MODEM_MODE_DATA);
    }
    
    return err;
}

esp_err_t lte_ppp_get_signal_quality(lte_ppp_handle_t handle, int* rssi, int* ber)
{
    if (!handle || !rssi || !ber) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return esp_modem_get_signal_quality(handle->dce, rssi, ber);
}

esp_err_t lte_ppp_deinit(lte_ppp_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔧 Deinitializing LTE PPP Native Module");
    
    // Stop connection first
    lte_ppp_stop(handle);
    
    // Unregister event handlers
    esp_event_handler_unregister(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &lte_ppp_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_PPP_GOT_IP, &lte_ppp_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_PPP_LOST_IP, &lte_ppp_event_handler);
    
    // Destroy DCE
    if (handle->dce) {
        esp_modem_destroy(handle->dce);
    }
    
    // Destroy network interface
    if (handle->esp_netif) {
        esp_netif_destroy(handle->esp_netif);
    }
    
    // Delete mutex
    if (handle->state_mutex) {
        vSemaphoreDelete(handle->state_mutex);
    }
    
    free(handle);
    
    ESP_LOGI(TAG, "✅ LTE PPP Native Module deinitialized");
    
    return ESP_OK;
}

// Private helper functions

static esp_err_t lte_ppp_configure_modem(lte_ppp_handle_t handle)
{
    ESP_LOGI(TAG, "🔧 Configuring modem for PPP connection...");
    
    esp_err_t err;
    
    // Check if modem is responsive
    err = esp_modem_sync(handle->dce);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Modem sync failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set APN (this is handled by DCE config, but we can verify)
    ESP_LOGI(TAG, "📡 APN configured: %s", handle->config.apn);
    
    // Additional SIM7670G specific configuration can go here
    // For example, setting network mode, bands, etc.
    
    ESP_LOGI(TAG, "✅ Modem configured for PPP");
    
    return ESP_OK;
}

static void lte_ppp_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    lte_ppp_handle_t handle = (lte_ppp_handle_t)arg;
    
    if (event_base == NETIF_PPP_STATUS) {
        switch (event_id) {
            case NETIF_PPP_PHASE_NETWORK:
                ESP_LOGI(TAG, "🌐 PPP connection established");
                xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
                handle->state = LTE_PPP_STATE_CONNECTED;
                xSemaphoreGive(handle->state_mutex);
                
                if (handle->event_callback) {
                    handle->event_callback(LTE_PPP_EVENT_CONNECTED, event_data, handle->user_data);
                }
                break;
                
            case NETIF_PPP_PHASE_DISCONNECT:
                ESP_LOGI(TAG, "🔌 PPP connection lost");
                xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
                handle->state = LTE_PPP_STATE_IDLE;
                xSemaphoreGive(handle->state_mutex);
                
                if (handle->event_callback) {
                    handle->event_callback(LTE_PPP_EVENT_DISCONNECTED, event_data, handle->user_data);
                }
                break;
                
            default:
                ESP_LOGD(TAG, "📊 PPP event: %ld", event_id);
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_PPP_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "🎯 Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            
            if (handle->event_callback) {
                handle->event_callback(LTE_PPP_EVENT_GOT_IP, event_data, handle->user_data);
            }
        } else if (event_id == IP_EVENT_PPP_LOST_IP) {
            ESP_LOGI(TAG, "📡 Lost IP address");
            
            if (handle->event_callback) {
                handle->event_callback(LTE_PPP_EVENT_LOST_IP, event_data, handle->user_data);
            }
        }
    }
}

static void lte_ppp_monitor_task(void* pvParameters)
{
    lte_ppp_handle_t handle = (lte_ppp_handle_t)pvParameters;
    
    ESP_LOGI(TAG, "🔍 PPP monitor task started");
    
    while (handle->monitor_running) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
        
        if (!handle->monitor_running) {
            break;
        }
        
        // Check connection status
        lte_ppp_state_t state = lte_ppp_get_state(handle);
        
        if (state == LTE_PPP_STATE_CONNECTED) {
            // Connection is good, check signal quality
            int rssi, ber;
            if (lte_ppp_get_signal_quality(handle, &rssi, &ber) == ESP_OK) {
                ESP_LOGD(TAG, "📶 Signal: RSSI=%d dBm, BER=%d", rssi, ber);
            }
        } else if (state == LTE_PPP_STATE_IDLE && handle->config.auto_reconnect) {
            // Try to reconnect
            ESP_LOGI(TAG, "🔄 Attempting auto-reconnect...");
            
            if (handle->event_callback) {
                handle->event_callback(LTE_PPP_EVENT_RECONNECTING, NULL, handle->user_data);
            }
            
            lte_ppp_start(handle);
        }
    }
    
    ESP_LOGI(TAG, "🔍 PPP monitor task stopped");
    vTaskDelete(NULL);
}