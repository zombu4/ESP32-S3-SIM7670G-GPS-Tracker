#include "cellular_native_stack.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "CELLULAR_NATIVE_STACK";

/**
 * @brief Stack handle structure
 */
struct cellular_native_handle_s {
    // Sub-modules
    lte_ppp_handle_t lte_handle;
    mqtt_native_handle_t mqtt_handle;
    
    // Configuration
    cellular_native_config_t config;
    
    // State
    bool lte_connected;
    bool mqtt_connected;
    SemaphoreHandle_t state_mutex;
    
    // Event handling
    cellular_native_event_cb_t event_callback;
    void* user_data;
    
    // Monitoring task
    TaskHandle_t monitor_task;
    bool monitor_running;
};

// Forward declarations
static void lte_event_callback(lte_ppp_event_t event, void* event_data, void* user_data);
static void mqtt_event_callback(mqtt_native_event_t event, mqtt_native_data_t* data, void* user_data);
static void stack_monitor_task(void* pvParameters);
static void notify_event(cellular_native_handle_t handle, cellular_native_event_t event, void* event_data);

esp_err_t cellular_native_init(const cellular_native_config_t* config, cellular_native_handle_t* handle)
{
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🚀 Initializing Cellular Native Stack");
    ESP_LOGI(TAG, "📡 LTE APN: %s", config->lte_config.apn);
    ESP_LOGI(TAG, "🌐 MQTT Broker: %s", config->mqtt_config.broker_uri);
    
    // Allocate handle
    cellular_native_handle_t h = calloc(1, sizeof(struct cellular_native_handle_s));
    if (!h) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for handle");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    memcpy(&h->config, config, sizeof(cellular_native_config_t));
    
    // Create state mutex
    h->state_mutex = xSemaphoreCreateMutex();
    if (!h->state_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create state mutex");
        free(h);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize LTE PPP module
    esp_err_t err = lte_ppp_init(&config->lte_config, &h->lte_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize LTE PPP: %s", esp_err_to_name(err));
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return err;
    }
    
    // Initialize MQTT module
    err = mqtt_native_init(&config->mqtt_config, &h->mqtt_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize MQTT: %s", esp_err_to_name(err));
        lte_ppp_deinit(h->lte_handle);
        vSemaphoreDelete(h->state_mutex);
        free(h);
        return err;
    }
    
    // Register event callbacks
    lte_ppp_register_event_cb(h->lte_handle, lte_event_callback, h);
    mqtt_native_register_event_cb(h->mqtt_handle, mqtt_event_callback, h);
    
    ESP_LOGI(TAG, "✅ Cellular Native Stack initialized successfully");
    
    *handle = h;
    return ESP_OK;
}

esp_err_t cellular_native_start(cellular_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Starting Cellular Native Stack...");
    
    // Start LTE PPP connection
    esp_err_t err = lte_ppp_start(handle->lte_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start LTE PPP: %s", esp_err_to_name(err));
        return err;
    }
    
    // Start monitoring task
    handle->monitor_running = true;
    xTaskCreate(stack_monitor_task, "cellular_monitor", 4096, handle, 5, &handle->monitor_task);
    
    ESP_LOGI(TAG, "🎯 Cellular Native Stack started");
    
    return ESP_OK;
}

esp_err_t cellular_native_stop(cellular_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔌 Stopping Cellular Native Stack...");
    
    // Stop monitoring task
    if (handle->monitor_task) {
        handle->monitor_running = false;
        vTaskDelete(handle->monitor_task);
        handle->monitor_task = NULL;
    }
    
    // Disconnect MQTT
    if (handle->mqtt_connected) {
        mqtt_native_disconnect(handle->mqtt_handle);
    }
    
    // Stop LTE
    lte_ppp_stop(handle->lte_handle);
    
    // Update state
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    handle->lte_connected = false;
    handle->mqtt_connected = false;
    xSemaphoreGive(handle->state_mutex);
    
    ESP_LOGI(TAG, "✅ Cellular Native Stack stopped");
    
    return ESP_OK;
}

lte_ppp_handle_t cellular_native_get_lte_handle(cellular_native_handle_t handle)
{
    return handle ? handle->lte_handle : NULL;
}

mqtt_native_handle_t cellular_native_get_mqtt_handle(cellular_native_handle_t handle)
{
    return handle ? handle->mqtt_handle : NULL;
}

int cellular_native_publish_gps_data(cellular_native_handle_t handle, 
                                    double latitude, double longitude, double altitude, 
                                    float battery_voltage)
{
    if (!handle || !cellular_native_is_ready(handle)) {
        ESP_LOGW(TAG, "⚠️ Stack not ready for GPS publish");
        return -1;
    }
    
    // Create JSON payload
    cJSON* json = cJSON_CreateObject();
    cJSON* gps = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(gps, "lat", latitude);
    cJSON_AddNumberToObject(gps, "lon", longitude);
    cJSON_AddNumberToObject(gps, "alt", altitude);
    cJSON_AddItemToObject(json, "gps", gps);
    
    cJSON_AddNumberToObject(json, "battery", battery_voltage);
    cJSON_AddNumberToObject(json, "timestamp", esp_timer_get_time() / 1000000ULL);
    
    char* json_string = cJSON_Print(json);
    if (!json_string) {
        ESP_LOGE(TAG, "❌ Failed to create JSON payload");
        cJSON_Delete(json);
        return -1;
    }
    
    ESP_LOGI(TAG, "📡 Publishing GPS data: %s", json_string);
    
    // Publish to default topic
    int msg_id = mqtt_native_publish(handle->mqtt_handle, "gps_tracker/data", 
                                   json_string, 0, -1, -1);
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    
    return msg_id;
}

bool cellular_native_is_ready(cellular_native_handle_t handle)
{
    if (!handle) {
        return false;
    }
    
    xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
    bool ready = handle->lte_connected && handle->mqtt_connected;
    xSemaphoreGive(handle->state_mutex);
    
    return ready;
}

esp_err_t cellular_native_register_event_cb(cellular_native_handle_t handle, 
                                           cellular_native_event_cb_t callback, void* user_data)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->event_callback = callback;
    handle->user_data = user_data;
    
    return ESP_OK;
}

esp_err_t cellular_native_deinit(cellular_native_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔧 Deinitializing Cellular Native Stack");
    
    // Stop first
    cellular_native_stop(handle);
    
    // Deinitialize sub-modules
    if (handle->mqtt_handle) {
        mqtt_native_deinit(handle->mqtt_handle);
    }
    if (handle->lte_handle) {
        lte_ppp_deinit(handle->lte_handle);
    }
    
    // Delete mutex
    if (handle->state_mutex) {
        vSemaphoreDelete(handle->state_mutex);
    }
    
    free(handle);
    
    ESP_LOGI(TAG, "✅ Cellular Native Stack deinitialized");
    
    return ESP_OK;
}

// Private helper functions

static void lte_event_callback(lte_ppp_event_t event, void* event_data, void* user_data)
{
    cellular_native_handle_t handle = (cellular_native_handle_t)user_data;
    
    switch (event) {
        case LTE_PPP_EVENT_CONNECTED:
            ESP_LOGI(TAG, "📶 LTE PPP connected");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->lte_connected = true;
            xSemaphoreGive(handle->state_mutex);
            
            notify_event(handle, CELLULAR_NATIVE_EVENT_LTE_CONNECTED, event_data);
            
            // Auto-connect MQTT if configured
            if (handle->config.auto_connect_mqtt) {
                ESP_LOGI(TAG, "🚀 Auto-connecting MQTT...");
                mqtt_native_connect(handle->mqtt_handle);
            }
            break;
            
        case LTE_PPP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "📵 LTE PPP disconnected");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->lte_connected = false;
            handle->mqtt_connected = false; // MQTT can't work without LTE
            xSemaphoreGive(handle->state_mutex);
            
            notify_event(handle, CELLULAR_NATIVE_EVENT_LTE_DISCONNECTED, event_data);
            break;
            
        case LTE_PPP_EVENT_GOT_IP:
            ESP_LOGI(TAG, "🌐 Got IP address via LTE PPP");
            break;
            
        default:
            ESP_LOGD(TAG, "📊 LTE event: %d", event);
            break;
    }
}

static void mqtt_event_callback(mqtt_native_event_t event, mqtt_native_data_t* data, void* user_data)
{
    cellular_native_handle_t handle = (cellular_native_handle_t)user_data;
    
    switch (event) {
        case MQTT_NATIVE_EVENT_CONNECTED:
            ESP_LOGI(TAG, "🌐 MQTT connected via native TCP");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->mqtt_connected = true;
            bool stack_ready = handle->lte_connected && handle->mqtt_connected;
            xSemaphoreGive(handle->state_mutex);
            
            notify_event(handle, CELLULAR_NATIVE_EVENT_MQTT_CONNECTED, data);
            
            if (stack_ready) {
                ESP_LOGI(TAG, "🎉 Cellular Native Stack fully ready!");
                notify_event(handle, CELLULAR_NATIVE_EVENT_STACK_READY, NULL);
            }
            break;
            
        case MQTT_NATIVE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "🔌 MQTT disconnected");
            
            xSemaphoreTake(handle->state_mutex, portMAX_DELAY);
            handle->mqtt_connected = false;
            xSemaphoreGive(handle->state_mutex);
            
            notify_event(handle, CELLULAR_NATIVE_EVENT_MQTT_DISCONNECTED, data);
            break;
            
        case MQTT_NATIVE_EVENT_DATA:
            ESP_LOGI(TAG, "📥 MQTT data received");
            notify_event(handle, CELLULAR_NATIVE_EVENT_MQTT_DATA, data);
            break;
            
        case MQTT_NATIVE_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ MQTT error");
            notify_event(handle, CELLULAR_NATIVE_EVENT_ERROR, data);
            break;
            
        default:
            ESP_LOGD(TAG, "📊 MQTT event: %d", event);
            break;
    }
}

static void stack_monitor_task(void* pvParameters)
{
    cellular_native_handle_t handle = (cellular_native_handle_t)pvParameters;
    
    ESP_LOGI(TAG, "🔍 Stack monitor task started");
    
    while (handle->monitor_running) {
        vTaskDelay(pdMS_TO_TICKS(handle->config.connection_check_ms));
        
        if (!handle->monitor_running) {
            break;
        }
        
        // Check LTE status
        lte_ppp_state_t lte_state = lte_ppp_get_state(handle->lte_handle);
        mqtt_native_state_t mqtt_state = mqtt_native_get_state(handle->mqtt_handle);
        
        ESP_LOGI(TAG, "📊 Status - LTE: %d, MQTT: %d, Stack Ready: %s", 
                 lte_state, mqtt_state, cellular_native_is_ready(handle) ? "YES" : "NO");
        
        // Auto-reconnect logic could go here
        if (handle->lte_connected && !handle->mqtt_connected && handle->config.auto_connect_mqtt) {
            if (mqtt_state == MQTT_NATIVE_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "🔄 Attempting MQTT reconnect...");
                mqtt_native_connect(handle->mqtt_handle);
            }
        }
    }
    
    ESP_LOGI(TAG, "🔍 Stack monitor task stopped");
    vTaskDelete(NULL);
}

static void notify_event(cellular_native_handle_t handle, cellular_native_event_t event, void* event_data)
{
    if (handle->event_callback) {
        handle->event_callback(event, event_data, handle->user_data);
    }
}