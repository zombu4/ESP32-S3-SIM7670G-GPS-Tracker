#include "mqtt_native_tcp.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "MQTT_NATIVE_TCP";

/**
 * @brief MQTT handle structure
 */
struct mqtt_native_handle_s {
    // ESP MQTT client
    esp_mqtt_client_handle_t client;
    
    // Configuration
    mqtt_native_config_t config;
    char* broker_uri_buf;  // Allocated buffer for URI
    char* client_id_buf;   // Allocated buffer for client ID
    
    // State management
    mqtt_native_state_t state;
    SemaphoreHandle_t state_mutex;
    esp_err_t last_error;
    
    // Event handling
    mqtt_native_event_cb_t event_callback;
    void* user_data;
    
    // Statistics
    uint32_t messages_sent;
    uint32_t messages_received;
};

// Forward declarations
static void mqtt_native_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
static char* generate_client_id(void);

esp_err_t mqtt_native_init(const mqtt_native_config_t* config, mqtt_native_handle_t* handle)
{
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🚀 Initializing Native TCP MQTT Module");
    ESP_LOGI(TAG, "🌐 Broker: %s", config->broker_uri);
    ESP_LOGI(TAG, "🔐 TLS: %s", config->use_tls ? "Enabled" : "Disabled");
    
    // Allocate handle
    mqtt_native_handle_t h = calloc(1, sizeof(struct mqtt_native_handle_s));
    if (!h) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for handle");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    memcpy(&h->config, config, sizeof(mqtt_native_config_t));
    h->state = MQTT_NATIVE_STATE_DISCONNECTED;
    h->last_error = ESP_OK;
    
    // Create state mutex
    h->state_mutex = xSemaphoreCreateMutex();
    if (!h->state_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create state mutex");
        free(h);
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate and copy broker URI
    size_t uri_len = strlen(config->broker_uri) + 1;
    h->broker_uri_buf = malloc(uri_len);
    if (!h->broker_uri_buf) {
        ESP_LOGE(TAG, "❌ Failed to allocate URI buffer");
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return ESP_ERR_NO_MEM;
    }
    strcpy(h->broker_uri_buf, config->broker_uri);
    
    // Generate or copy client ID
    if (config->client_id) {
        size_t id_len = strlen(config->client_id) + 1;
        h->client_id_buf = malloc(id_len);
        if (!h->client_id_buf) {
            ESP_LOGE(TAG, "❌ Failed to allocate client ID buffer");
            free(h->broker_uri_buf);
            vSemaphoreDelete(h->state_mutex);
            free(h);
            return ESP_ERR_NO_MEM;
        }
        strcpy(h->client_id_buf, config->client_id);
    } else {
        h->client_id_buf = generate_client_id();
        if (!h->client_id_buf) {
            ESP_LOGE(TAG, "❌ Failed to generate client ID");
            free(h->broker_uri_buf);
            vSemaphoreDelete(h->state_mutex);
            free(h);
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = h->broker_uri_buf,
        .credentials.client_id = h->client_id_buf,
        .credentials.username = config->username,
        .credentials.authentication.password = config->password,
        .session.keepalive = config->keepalive,
        .session.disable_clean_session = !config->clean_session,
        .network.timeout_ms = config->network_timeout_ms,
        .network.reconnect_timeout_ms = config->reconnect_timeout_ms,
    };
    
    // TLS configuration
    if (config->use_tls) {
        if (config->cert_pem) {
            mqtt_cfg.broker.verification.certificate = config->cert_pem;
        } else {
            mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
        }
    }
    
    // Create MQTT client
    h->client = esp_mqtt_client_init(&mqtt_cfg);
    if (!h->client) {
        ESP_LOGE(TAG, "❌ Failed to initialize MQTT client");
        free(h->client_id_buf);
        free(h->broker_uri_buf);
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return ESP_FAIL;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(h->client, ESP_EVENT_ANY_ID, mqtt_native_event_handler, h);
    
    ESP_LOGI(TAG, "✅ Native TCP MQTT Module initialized successfully");
    ESP_LOGI(TAG, "🔖 Client ID: %s", h->client_id_buf);
    
    *handle = h;
    
    return ESP_OK;
}

esp_err_t mqtt_native_connect(mqtt_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Connecting to MQTT broker...");
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    
    if (handle->state != MQTT_NATIVE_STATE_DISCONNECTED) {
        ESP_LOGW(TAG, "⚠️ Already connecting/connected (state: %d)", handle->state);
        xSemaphoreGive(handle->state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    handle->state = MQTT_NATIVE_STATE_CONNECTING;
    xSemaphoreGive(handle->state_mutex);
    
    // Start MQTT client
    esp_err_t err = esp_mqtt_client_start(handle->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start MQTT client: %s", esp_err_to_name(err));
        
        xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
        handle->state = MQTT_NATIVE_STATE_ERROR;
        handle->last_error = err;
        xSemaphoreGive(handle->state_mutex);
        
        return err;
    }
    
    ESP_LOGI(TAG, "🎯 MQTT connection initiated");
    
    return ESP_OK;
}

esp_err_t mqtt_native_disconnect(mqtt_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Disconnecting from MQTT broker...");
    
    esp_err_t err = esp_mqtt_client_stop(handle->client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ Failed to stop MQTT client: %s", esp_err_to_name(err));
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    handle->state = MQTT_NATIVE_STATE_DISCONNECTED;
    xSemaphoreGive(handle->state_mutex);
    
    ESP_LOGI(TAG, "✅ Disconnected from MQTT broker");
    
    return ESP_OK;
}

mqtt_native_state_t mqtt_native_get_state(mqtt_native_handle_t handle)
{
    if (!handle) {
        return MQTT_NATIVE_STATE_ERROR;
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    mqtt_native_state_t state = handle->state;
    xSemaphoreGive(handle->state_mutex);
    
    return state;
}

int mqtt_native_publish(mqtt_native_handle_t handle, const char* topic, 
                       const char* data, int data_len, int qos, int retain)
{
    if (!handle || !topic || !data) {
        return -1;
    }
    
    if (handle->state != MQTT_NATIVE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "⚠️ Not connected, cannot publish");
        return -1;
    }
    
    // Use defaults if not specified
    if (qos < 0) qos = handle->config.qos;
    if (retain < 0) retain = handle->config.retain;
    if (data_len == 0) data_len = strlen(data);
    
    ESP_LOGD(TAG, "📤 Publishing to '%s' (len=%d, qos=%d, retain=%d)", topic, data_len, qos, retain);
    
    int msg_id = esp_mqtt_client_publish(handle->client, topic, data, data_len, qos, retain);
    
    if (msg_id >= 0) {
        handle->messages_sent++;
        ESP_LOGD(TAG, "✅ Message published (ID: %d)", msg_id);
    } else {
        ESP_LOGE(TAG, "❌ Failed to publish message");
    }
    
    return msg_id;
}

int mqtt_native_subscribe(mqtt_native_handle_t handle, const char* topic, int qos)
{
    if (!handle || !topic) {
        return -1;
    }
    
    if (handle->state != MQTT_NATIVE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "⚠️ Not connected, cannot subscribe");
        return -1;
    }
    
    // Use default if not specified
    if (qos < 0) qos = handle->config.qos;
    
    ESP_LOGI(TAG, "📥 Subscribing to '%s' (qos=%d)", topic, qos);
    
    int msg_id = esp_mqtt_client_subscribe(handle->client, topic, qos);
    
    if (msg_id >= 0) {
        ESP_LOGD(TAG, "✅ Subscription request sent (ID: %d)", msg_id);
    } else {
        ESP_LOGE(TAG, "❌ Failed to send subscription request");
    }
    
    return msg_id;
}

int mqtt_native_unsubscribe(mqtt_native_handle_t handle, const char* topic)
{
    if (!handle || !topic) {
        return -1;
    }
    
    if (handle->state != MQTT_NATIVE_STATE_CONNECTED) {
        ESP_LOGW(TAG, "⚠️ Not connected, cannot unsubscribe");
        return -1;
    }
    
    ESP_LOGI(TAG, "📤 Unsubscribing from '%s'", topic);
    
    int msg_id = esp_mqtt_client_unsubscribe(handle->client, topic);
    
    if (msg_id >= 0) {
        ESP_LOGD(TAG, "✅ Unsubscription request sent (ID: %d)", msg_id);
    } else {
        ESP_LOGE(TAG, "❌ Failed to send unsubscription request");
    }
    
    return msg_id;
}

esp_err_t mqtt_native_register_event_cb(mqtt_native_handle_t handle, mqtt_native_event_cb_t callback, void* user_data)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->event_callback = callback;
    handle->user_data = user_data;
    
    return ESP_OK;
}

bool mqtt_native_is_connected(mqtt_native_handle_t handle)
{
    if (!handle) {
        return false;
    }
    
    return (mqtt_native_get_state(handle) == MQTT_NATIVE_STATE_CONNECTED);
}

esp_err_t mqtt_native_get_last_error(mqtt_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    esp_err_t error = handle->last_error;
    xSemaphoreGive(handle->state_mutex);
    
    return error;
}

esp_err_t mqtt_native_deinit(mqtt_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔧 Deinitializing Native TCP MQTT Module");
    ESP_LOGI(TAG, "📊 Messages sent: %ld, received: %ld", handle->messages_sent, handle->messages_received);
    
    // Disconnect first
    mqtt_native_disconnect(handle);
    
    // Destroy MQTT client
    if (handle->client) {
        esp_mqtt_client_destroy(handle->client);
    }
    
    // Free allocated memory
    if (handle->client_id_buf) {
        free(handle->client_id_buf);
    }
    if (handle->broker_uri_buf) {
        free(handle->broker_uri_buf);
    }
    
    // Delete mutex
    if (handle->state_mutex) {
        vSemaphoreDelete(handle->state_mutex);
    }
    
    free(handle);
    
    ESP_LOGI(TAG, "✅ Native TCP MQTT Module deinitialized");
    
    return ESP_OK;
}

// Private helper functions

static char* generate_client_id(void)
{
    char* client_id = malloc(32);
    if (!client_id) {
        return NULL;
    }
    
    uint32_t chip_id = 0;
    for (int i = 0; i < 17; i = i + 8) {
        chip_id |= ((esp_random() & 0xff) << i);
    }
    
    snprintf(client_id, 32, "esp32_gps_%08lx", chip_id);
    
    return client_id;
}

static void mqtt_native_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    mqtt_native_handle_t handle = (mqtt_native_handle_t)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "🌐 Connected to MQTT broker");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->state = MQTT_NATIVE_STATE_CONNECTED;
            handle->last_error = ESP_OK;
            xSemaphoreGive(handle->state_mutex);
            
            if (handle->event_callback) {
                handle->event_callback(MQTT_NATIVE_EVENT_CONNECTED, NULL, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "🔌 Disconnected from MQTT broker");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->state = MQTT_NATIVE_STATE_DISCONNECTED;
            xSemaphoreGive(handle->state_mutex);
            
            if (handle->event_callback) {
                handle->event_callback(MQTT_NATIVE_EVENT_DISCONNECTED, NULL, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "📥 Successfully subscribed (msg_id=%d)", event->msg_id);
            
            if (handle->event_callback) {
                mqtt_native_data_t data = {
                    .msg_id = event->msg_id,
                };
                handle->event_callback(MQTT_NATIVE_EVENT_SUBSCRIBED, &data, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "📤 Successfully unsubscribed (msg_id=%d)", event->msg_id);
            
            if (handle->event_callback) {
                mqtt_native_data_t data = {
                    .msg_id = event->msg_id,
                };
                handle->event_callback(MQTT_NATIVE_EVENT_UNSUBSCRIBED, &data, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "📤 Message published successfully (msg_id=%d)", event->msg_id);
            
            if (handle->event_callback) {
                mqtt_native_data_t data = {
                    .msg_id = event->msg_id,
                };
                handle->event_callback(MQTT_NATIVE_EVENT_PUBLISHED, &data, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "📥 Data received: topic=%.*s, data=%.*s", 
                     event->topic_len, event->topic, event->data_len, event->data);
            
            handle->messages_received++;
            
            if (handle->event_callback) {
                mqtt_native_data_t data = {
                    .topic = event->topic,
                    .data = event->data,
                    .data_len = event->data_len,
                    .msg_id = event->msg_id,
                    .qos = event->qos,
                    .retain = event->retain,
                };
                handle->event_callback(MQTT_NATIVE_EVENT_DATA, &data, handle->user_data);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ MQTT error occurred");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->state = MQTT_NATIVE_STATE_ERROR;
            handle->last_error = ESP_FAIL;
            xSemaphoreGive(handle->state_mutex);
            
            if (handle->event_callback) {
                handle->event_callback(MQTT_NATIVE_EVENT_ERROR, NULL, handle->user_data);
            }
            break;
            
        default:
            ESP_LOGD(TAG, "📊 MQTT event: %ld", event_id);
            break;
    }
}