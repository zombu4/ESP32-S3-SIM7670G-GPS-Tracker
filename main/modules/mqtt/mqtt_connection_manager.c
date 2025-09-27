/**
 * 📡 MQTT CONNECTION MANAGER MODULE IMPLEMENTATION
 * 
 * Handles MQTT connection establishment, monitoring, and recovery
 * Separate module for easy debugging and testing
 */

#include "mqtt_connection_manager.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include <inttypes.h> 
#include <string.h>

static const char *TAG = "MQTT_CONN_MGR";

// 📡 INTERNAL STATE 📡

static mqtt_connection_config_t g_config = {0};
static mqtt_connection_stats_t g_stats = {0};
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static bool g_initialized = false;
static TaskHandle_t g_process_task = NULL;

// Internal flags
#define MQTT_CONNECTED_BIT     BIT0
#define MQTT_DISCONNECT_BIT    BIT1
static EventGroupHandle_t g_mqtt_event_group = NULL;

// 📡 FORWARD DECLARATIONS 📡

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_process_task(void *pvParameters);
static void update_connection_stats(bool connected, int error_code, const char* error_msg);

// 📡 INTERFACE IMPLEMENTATIONS 📡

static bool mqtt_initialize_impl(const mqtt_connection_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "❌ NULL configuration provided");
        return false;
    }
    
    if (g_initialized) {
        ESP_LOGW(TAG, "⚠️ Already initialized, reinitializing...");
        // Cleanup existing resources
        if (g_mqtt_client) {
            esp_mqtt_client_destroy(g_mqtt_client);
            g_mqtt_client = NULL;
        }
    }
    
    // Copy configuration
    memcpy(&g_config, config, sizeof(mqtt_connection_config_t));
    
    // Reset statistics
    memset(&g_stats, 0, sizeof(mqtt_connection_stats_t));
    g_stats.status = MQTT_STATUS_DISCONNECTED;
    
    // Create event group if not exists
    if (!g_mqtt_event_group) {
        g_mqtt_event_group = xEventGroupCreate();
        if (!g_mqtt_event_group) {
            ESP_LOGE(TAG, "❌ Failed to create event group");
            return false;
        }
    }
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = NULL, // Will be set below
        .broker.address.hostname = g_config.broker_host,
        .broker.address.port = g_config.broker_port,
        .credentials.client_id = g_config.client_id,
        .credentials.username = g_config.username,
        .credentials.authentication.password = g_config.password,
        .session.keepalive = g_config.keepalive_seconds,
        .network.timeout_ms = g_config.connect_timeout_ms,
        .network.refresh_connection_after_ms = 30000,
        .network.disable_auto_reconnect = true, // We handle reconnection manually
    };
    
    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_mqtt_client) {
        ESP_LOGE(TAG, "❌ Failed to initialize MQTT client");
        return false;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    // Create processing task if not exists
    if (!g_process_task) {
        BaseType_t result = xTaskCreate(
            mqtt_process_task,
            "mqtt_process",
            4096,
            NULL,
            5,
            &g_process_task
        );
        
        if (result != pdPASS) {
            ESP_LOGE(TAG, "❌ Failed to create process task");
            esp_mqtt_client_destroy(g_mqtt_client);
            g_mqtt_client = NULL;
            return false;
        }
    }
    
    g_initialized = true;
    
    ESP_LOGI(TAG, "✅ MQTT Connection Manager initialized: %s:%d", 
             g_config.broker_host, g_config.broker_port);
    
    if (g_config.debug_enabled) {
        ESP_LOGI(TAG, "🐛 Debug enabled - client_id: %s, keepalive: %us", 
                 g_config.client_id ? g_config.client_id : "auto",
                 g_config.keepalive_seconds);
    }
    
    return true;
}

static bool mqtt_connect_impl(void)
{
    if (!g_initialized || !g_mqtt_client) {
        ESP_LOGE(TAG, "❌ Not initialized");
        return false;
    }
    
    if (g_stats.status == MQTT_STATUS_CONNECTED) {
        ESP_LOGW(TAG, "⚠️ Already connected");
        return true;
    }
    
    if (g_stats.status == MQTT_STATUS_CONNECTING) {
        ESP_LOGW(TAG, "⚠️ Connection already in progress");
        return true;
    }
    
    ESP_LOGI(TAG, "🔄 Connecting to MQTT broker %s:%d...", 
             g_config.broker_host, g_config.broker_port);
    
    g_stats.connection_attempts++;
    g_stats.status = MQTT_STATUS_CONNECTING;
    
    esp_err_t err = esp_mqtt_client_start(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start MQTT client: %s", esp_err_to_name(err));
        update_connection_stats(false, err, esp_err_to_name(err));
        return false;
    }
    
    return true;
}

static bool mqtt_disconnect_impl(void)
{
    if (!g_initialized || !g_mqtt_client) {
        return true; // Consider already disconnected
    }
    
    ESP_LOGI(TAG, "🔄 Disconnecting from MQTT broker...");
    
    esp_err_t err = esp_mqtt_client_stop(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to stop MQTT client: %s", esp_err_to_name(err));
        return false;
    }
    
    // Signal disconnection
    if (g_mqtt_event_group) {
        xEventGroupSetBits(g_mqtt_event_group, MQTT_DISCONNECT_BIT);
        xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);
    }
    
    g_stats.status = MQTT_STATUS_DISCONNECTED;
    g_stats.disconnection_count++;
    
    return true;
}

static bool mqtt_is_connected_impl(void)
{
    return g_initialized && (g_stats.status == MQTT_STATUS_CONNECTED);
}

static mqtt_connection_status_t mqtt_get_status_impl(void)
{
    return g_stats.status;
}

static void mqtt_process_impl(void)
{
    // This is handled by the background task
    // Just update uptime here
    static uint32_t last_update = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (now - last_update > 1000) { // Update every second
        if (g_stats.status == MQTT_STATUS_CONNECTED && g_stats.last_connection_time > 0) {
            g_stats.uptime_seconds = (now - g_stats.last_connection_time) / 1000;
        }
        last_update = now;
    }
}

static bool mqtt_reconnect_impl(void)
{
    if (!g_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "🔄 Force reconnecting...");
    
    // Disconnect first
    mqtt_disconnect_impl();
    
    // Wait a moment
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Reconnect
    return mqtt_connect_impl();
}

static void mqtt_get_stats_impl(mqtt_connection_stats_t* stats)
{
    if (stats) {
        memcpy(stats, &g_stats, sizeof(mqtt_connection_stats_t));
    }
}

static void mqtt_get_debug_info_impl(char* debug_str, size_t max_len)
{
    if (!debug_str) return;
    
    const char* status_str[] = {
        "DISCONNECTED", "CONNECTING", "CONNECTED", 
        "PUBLISHING", "ERROR", "RECONNECTING"
    };
    
    snprintf(debug_str, max_len,
        "MQTT: status=%s, attempts=%" PRIu32 ", success=%" PRIu32 ", failures=%" PRIu32 ", uptime=%" PRIu32 "s, msgs=%" PRIu32,
        status_str[g_stats.status],
        g_stats.connection_attempts,
        g_stats.successful_connections, 
        g_stats.connection_failures,
        g_stats.uptime_seconds,
        g_stats.messages_published);
}

static void mqtt_reset_stats_impl(void)
{
    mqtt_connection_status_t current_status = g_stats.status;
    memset(&g_stats, 0, sizeof(mqtt_connection_stats_t));
    g_stats.status = current_status;
    
    ESP_LOGI(TAG, "📊 Statistics reset");
}

static bool mqtt_update_config_impl(const mqtt_connection_config_t* config)
{
    if (!config) return false;
    
    memcpy(&g_config, config, sizeof(mqtt_connection_config_t));
    ESP_LOGI(TAG, "⚙️ Configuration updated - reconnection required");
    
    return true;
}

// 📡 EVENT HANDLER 📡

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ MQTT Connected to broker");
            update_connection_stats(true, 0, "Connected");
            if (g_mqtt_event_group) {
                xEventGroupSetBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);
                xEventGroupClearBits(g_mqtt_event_group, MQTT_DISCONNECT_BIT);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "⚠️ MQTT Disconnected from broker");
            update_connection_stats(false, 0, "Disconnected");
            if (g_mqtt_event_group) {
                xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);
                xEventGroupSetBits(g_mqtt_event_group, MQTT_DISCONNECT_BIT);
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            g_stats.messages_published++;
            if (g_config.debug_enabled) {
                ESP_LOGD(TAG, "📤 Message published, msg_id=%d", event->msg_id);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ MQTT Error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: 0x%x", event->error_handle->esp_tls_last_esp_err);
                update_connection_stats(false, event->error_handle->esp_tls_last_esp_err, "TCP Error");
            } else {
                update_connection_stats(false, -1, "MQTT Error");
            }
            break;
            
        default:
            if (g_config.debug_enabled) {
                ESP_LOGD(TAG, "🔄 MQTT Event: %d", event_id);
            }
            break;
    }
}

// 📡 BACKGROUND PROCESSING TASK 📡

static void mqtt_process_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🔄 MQTT Process Task started");
    
    TickType_t last_reconnect_attempt = 0;
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        
        // Handle automatic reconnection
        if (g_stats.status == MQTT_STATUS_DISCONNECTED || g_stats.status == MQTT_STATUS_ERROR) {
            if (now - last_reconnect_attempt > pdMS_TO_TICKS(g_config.retry_interval_ms)) {
                if (g_stats.connection_attempts < g_config.max_retry_attempts) {
                    ESP_LOGI(TAG, "🔄 Auto-reconnect attempt %u/%u", 
                             g_stats.connection_attempts + 1, g_config.max_retry_attempts);
                    mqtt_connect_impl();
                    last_reconnect_attempt = now;
                } else {
                    ESP_LOGW(TAG, "⚠️ Max reconnection attempts reached");
                    vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before trying again
                    g_stats.connection_attempts = 0; // Reset counter
                }
            }
        }
        
        // Update uptime
        mqtt_process_impl();
        
        // Wait before next iteration
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 📡 UTILITY FUNCTIONS 📡

static void update_connection_stats(bool connected, int error_code, const char* error_msg)
{
    if (connected) {
        g_stats.status = MQTT_STATUS_CONNECTED;
        g_stats.successful_connections++;
        g_stats.last_connection_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        g_stats.last_error_code = 0;
        memset(g_stats.last_error_message, 0, sizeof(g_stats.last_error_message));
    } else {
        g_stats.status = MQTT_STATUS_ERROR;
        g_stats.connection_failures++;
        g_stats.last_error_code = error_code;
        if (error_msg) {
            strncpy(g_stats.last_error_message, error_msg, sizeof(g_stats.last_error_message) - 1);
        }
    }
}

// 📡 INTERFACE STRUCTURE 📡

static const mqtt_connection_manager_interface_t mqtt_connection_manager_interface = {
    .initialize = mqtt_initialize_impl,
    .connect = mqtt_connect_impl,
    .disconnect = mqtt_disconnect_impl,
    .is_connected = mqtt_is_connected_impl,
    .get_status = mqtt_get_status_impl,
    .process = mqtt_process_impl,
    .reconnect = mqtt_reconnect_impl,
    .get_stats = mqtt_get_stats_impl,
    .get_debug_info = mqtt_get_debug_info_impl,
    .reset_stats = mqtt_reset_stats_impl,
    .update_config = mqtt_update_config_impl,
};

const mqtt_connection_manager_interface_t* mqtt_connection_manager_get_interface(void)
{
    return &mqtt_connection_manager_interface;
}

// 📡 DEFAULT CONFIGURATION 📡

mqtt_connection_config_t mqtt_connection_manager_get_default_config(void)
{
    mqtt_connection_config_t config = {
        .broker_host = "65.124.194.3",
        .broker_port = 1883,
        .client_id = "esp32_gps_tracker",
        .username = NULL,
        .password = NULL,
        .connect_timeout_ms = 10000,
        .keepalive_seconds = 60,
        .retry_interval_ms = 5000,
        .max_retry_attempts = 10,
        .debug_enabled = true,
    };
    
    return config;
}