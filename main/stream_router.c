#include "stream_router.h" 
#include "esp_log.h"
#include "esp_timer.h"
#include "string.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "STREAM_ROUTER";

// Stream router state
static struct {
    bool initialized;
    bool running;
    TaskHandle_t router_task;
    QueueHandle_t input_queue;
    stream_processor_callback_t processors[5];  // One for each stream type
    stream_router_stats_t stats;
    bool debug_enabled;
    SemaphoreHandle_t stats_mutex;
} router = {0};

// Stream pattern recognition
static bool is_gps_nmea_data(const char* data) {
    return (data[0] == '$' && (
        strncmp(data, "$GP", 3) == 0 ||  // GPS
        strncmp(data, "$GL", 3) == 0 ||  // GLONASS  
        strncmp(data, "$GA", 3) == 0 ||  // Galileo
        strncmp(data, "$GB", 3) == 0 ||  // BeiDou
        strncmp(data, "$GN", 3) == 0     // Multi-constellation
    ));
}

static bool is_lte_response(const char* data) {
    return (data[0] == '+' && (
        strncmp(data, "+CREG", 5) == 0 ||
        strncmp(data, "+CSQ", 4) == 0 ||
        strncmp(data, "+COPS", 5) == 0 ||
        strncmp(data, "+CGATT", 6) == 0 ||
        strncmp(data, "+CFUN", 5) == 0 ||
        strncmp(data, "+CPIN", 5) == 0
    ));
}

static bool is_mqtt_response(const char* data) {
    return (data[0] == '+' && strncmp(data, "+CMQTT", 6) == 0);
}

static bool is_at_status(const char* data) {
    return (strcmp(data, "OK") == 0 || 
            strcmp(data, "ERROR") == 0 || 
            strcmp(data, "READY") == 0 ||
            strncmp(data, "AT+", 3) == 0);
}

// Classify incoming data stream
static modem_stream_type_t classify_stream(const char* data) {
    if (!data || strlen(data) == 0) {
        return STREAM_TYPE_UNKNOWN;
    }

    if (is_gps_nmea_data(data)) {
        return STREAM_TYPE_GPS_NMEA;
    }
    
    if (is_lte_response(data)) {
        return STREAM_TYPE_LTE_RESPONSE;
    }
    
    if (is_mqtt_response(data)) {
        return STREAM_TYPE_MQTT_RESPONSE;
    }
    
    if (is_at_status(data)) {
        return STREAM_TYPE_AT_STATUS;
    }
    
    return STREAM_TYPE_UNKNOWN;
}

// Update routing statistics (thread-safe)
static void update_stats(modem_stream_type_t type) {
    if (xSemaphoreTake(router.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        router.stats.total_packets_routed++;
        
        switch (type) {
            case STREAM_TYPE_GPS_NMEA:
                router.stats.gps_packets++;
                break;
            case STREAM_TYPE_LTE_RESPONSE:
                router.stats.lte_packets++;
                break;
            case STREAM_TYPE_MQTT_RESPONSE:
                router.stats.mqtt_packets++;
                break;
            case STREAM_TYPE_AT_STATUS:
                router.stats.status_packets++;
                break;
            default:
                router.stats.unknown_packets++;
                break;
        }
        
        xSemaphoreGive(router.stats_mutex);
    }
}

// Main stream router task - runs on Core 0 (system management)
static void stream_router_task(void* parameters) {
    ESP_LOGI(TAG, "🚀 Stream Router started on Core %d", xPortGetCoreID());
    
    char uart_buffer[1024];
    modem_stream_packet_t packet;
    
    while (router.running) {
        // Read raw data from UART directly (we own the stream)
        int bytes_read = uart_read_bytes(UART_NUM_1, uart_buffer, sizeof(uart_buffer) - 1, pdMS_TO_TICKS(50));
        
        if (bytes_read > 0) {
                uart_buffer[bytes_read] = '\0';
                
                // Split buffer into individual lines/messages
                char* line = strtok(uart_buffer, "\r\n");
                while (line != NULL) {
                    if (strlen(line) > 0) {
                        // Classify the data stream
                        modem_stream_type_t type = classify_stream(line);
                        
                        // Create packet
                        memset(&packet, 0, sizeof(packet));
                        packet.type = type;
                        strncpy(packet.data, line, sizeof(packet.data) - 1);
                        packet.length = strlen(line);
                        packet.timestamp_ms = esp_timer_get_time() / 1000;
                        
                        // Set priority (GPS = highest, status = lowest)
                        switch (type) {
                            case STREAM_TYPE_GPS_NMEA:
                                packet.priority = 0;  // Highest - GPS timing critical
                                break;
                            case STREAM_TYPE_LTE_RESPONSE:
                                packet.priority = 1;  // High - Network connectivity
                                break;
                            case STREAM_TYPE_MQTT_RESPONSE:
                                packet.priority = 1;  // High - Data transmission
                                break;
                            case STREAM_TYPE_AT_STATUS:
                                packet.priority = 2;  // Normal - Status info
                                break;
                            default:
                                packet.priority = 3;  // Low - Unknown data
                                break;
                        }
                        
                        // Route to appropriate processor
                        if (router.processors[type] != NULL) {
                            // Call processor callback directly (parallel execution)
                            router.processors[type](&packet);
                            
                            if (router.debug_enabled) {
                                ESP_LOGI(TAG, "📨 Routed [%s] → Processor: %s", 
                                        (type == STREAM_TYPE_GPS_NMEA) ? "GPS" :
                                        (type == STREAM_TYPE_LTE_RESPONSE) ? "LTE" :
                                        (type == STREAM_TYPE_MQTT_RESPONSE) ? "MQTT" :
                                        (type == STREAM_TYPE_AT_STATUS) ? "STATUS" : "UNKNOWN",
                                        packet.data);
                            }
                        } else {
                            if (router.debug_enabled) {
                                ESP_LOGW(TAG, "⚠️  No processor for [%s]: %s", 
                                        (type == STREAM_TYPE_GPS_NMEA) ? "GPS" :
                                        (type == STREAM_TYPE_LTE_RESPONSE) ? "LTE" :
                                        (type == STREAM_TYPE_MQTT_RESPONSE) ? "MQTT" :
                                        (type == STREAM_TYPE_AT_STATUS) ? "STATUS" : "UNKNOWN",
                                        packet.data);
                            }
                        }
                        
                        // Update statistics
                        update_stats(type);
                    }
                    
                    line = strtok(NULL, "\r\n");
                }
            }
        
        // Small delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "🛑 Stream Router stopped");
    vTaskDelete(NULL);
}

// Implementation functions
static bool router_init(void) {
    if (router.initialized) {
        return true;
    }
    
    // Initialize UART for stream routing (we manage it centrally)
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 18, 17, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 4096, 4096, 0, NULL, 0));
    
    // Create statistics mutex
    router.stats_mutex = xSemaphoreCreateMutex();
    if (!router.stats_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create statistics mutex");
        return false;
    }
    
    // Initialize processor callbacks to NULL
    memset(router.processors, 0, sizeof(router.processors));
    
    // Initialize statistics
    memset(&router.stats, 0, sizeof(router.stats));
    
    router.initialized = true;
    ESP_LOGI(TAG, "✅ Stream Router initialized");
    return true;
}

static bool router_start(void) {
    if (!router.initialized) {
        ESP_LOGE(TAG, "❌ Router not initialized");
        return false;
    }
    
    if (router.running) {
        ESP_LOGW(TAG, "⚠️  Router already running");
        return true;
    }
    
    router.running = true;
    
    // Create router task on Core 0 (system management core)
    BaseType_t result = xTaskCreatePinnedToCore(
        stream_router_task,
        "stream_router",
        8192,  // 8KB stack
        NULL,
        24,    // High priority (same as system monitor)
        &router.router_task,
        0      // Core 0 - system management
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create router task");
        router.running = false;
        return false;
    }
    
    ESP_LOGI(TAG, "🚀 Stream Router started successfully");
    return true;
}

static void router_stop(void) {
    if (!router.running) {
        return;
    }
    
    router.running = false;
    
    if (router.router_task) {
        vTaskDelete(router.router_task);
        router.router_task = NULL;
    }
    
    ESP_LOGI(TAG, "🛑 Stream Router stopped");
}

static bool router_register_processor(modem_stream_type_t type, stream_processor_callback_t callback) {
    if (!router.initialized || type >= 5) {
        return false;
    }
    
    router.processors[type] = callback;
    
    ESP_LOGI(TAG, "📝 Registered processor for %s streams", 
            (type == STREAM_TYPE_GPS_NMEA) ? "GPS" :
            (type == STREAM_TYPE_LTE_RESPONSE) ? "LTE" :
            (type == STREAM_TYPE_MQTT_RESPONSE) ? "MQTT" :
            (type == STREAM_TYPE_AT_STATUS) ? "STATUS" : "UNKNOWN");
    
    return true;
}

static void router_get_stats(stream_router_stats_t* stats) {
    if (!stats || !router.stats_mutex) {
        return;
    }
    
    if (xSemaphoreTake(router.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &router.stats, sizeof(stream_router_stats_t));
        xSemaphoreGive(router.stats_mutex);
    }
}

static void router_set_debug(bool enable) {
    router.debug_enabled = enable;
    ESP_LOGI(TAG, "🔧 Debug output %s", enable ? "ENABLED" : "DISABLED");
}

// Interface definition
static const stream_router_interface_t router_interface = {
    .init = router_init,
    .start = router_start,
    .stop = router_stop,
    .register_processor = router_register_processor,
    .get_stats = router_get_stats,
    .set_debug = router_set_debug
};

const stream_router_interface_t* stream_router_get_interface(void) {
    return &router_interface;
}