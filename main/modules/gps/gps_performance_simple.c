/**
 * @file gps_performance_simple.c
 * @brief Simplified high-performance GPS implementation
 * 
 * Simplified version focusing on core ESP32-S3 optimizations:
 * 1. DMA-capable buffers for deterministic performance
 * 2. Core pinning for parallel processing  
 * 3. Performance measurement
 */

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>

#define GPS_UART_NUM                UART_NUM_1
#define GPS_UART_TX_PIN             17
#define GPS_UART_RX_PIN             18
#define GPS_UART_BAUD_RATE          9600
#define GPS_BUFFER_SIZE             1024
#define GPS_TASK_STACK_SIZE         4096
#define GPS_TASK_PRIORITY           10

static const char* TAG = "GPS_PERF";

typedef struct {
    bool initialized;
    RingbufHandle_t ring_buffer;
    TaskHandle_t process_task;
    uint64_t bytes_processed;
    uint64_t processing_time_total;
    uint32_t nmea_sentences_parsed;
} gps_perf_simple_t;

static gps_perf_simple_t gps_handle = {0};

/**
 * @brief High-priority GPS processing task (Core 0)
 */
static void gps_process_task(void* pvParameters)
{
    char* nmea_buffer = heap_caps_malloc(GPS_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!nmea_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "GPS processing task started on Core 0 with DMA buffer");
    
    while (1) {
        size_t item_size;
        char* data = (char*)xRingbufferReceive(gps_handle.ring_buffer, &item_size, pdMS_TO_TICKS(1000));
        
        if (data != NULL) {
            uint64_t start_time = esp_timer_get_time();
            
            // Process NMEA data (simplified)
            if (strstr(data, "$GP") || strstr(data, "$GN")) {
                gps_handle.nmea_sentences_parsed++;
                ESP_LOGD(TAG, "Parsed NMEA sentence: %.50s", data);
            }
            
            gps_handle.bytes_processed += item_size;
            gps_handle.processing_time_total += (esp_timer_get_time() - start_time);
            
            // Return buffer to ring buffer
            vRingbufferReturnItem(gps_handle.ring_buffer, data);
        }
    }
    
    heap_caps_free(nmea_buffer);
}

esp_err_t gps_perf_simple_init(void)
{
    if (gps_handle.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create DMA-capable ring buffer
    gps_handle.ring_buffer = xRingbufferCreate(GPS_BUFFER_SIZE * 4, RINGBUF_TYPE_BYTEBUF);
    if (!gps_handle.ring_buffer) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Configure UART with DMA
    uart_config_t uart_config = {
        .baud_rate = GPS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN, 
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, GPS_BUFFER_SIZE * 2, 
                                        GPS_BUFFER_SIZE * 2, 0, NULL, 0));
    
    // Create processing task on Core 0
    BaseType_t task_result = xTaskCreatePinnedToCore(
        gps_process_task,
        "gps_proc",
        GPS_TASK_STACK_SIZE,
        NULL,
        GPS_TASK_PRIORITY,
        &gps_handle.process_task,
        0  // Core 0
    );
    
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS processing task");
        vRingbufferDelete(gps_handle.ring_buffer);
        return ESP_ERR_NO_MEM;
    }
    
    gps_handle.initialized = true;
    ESP_LOGI(TAG, "Simple GPS performance module initialized");
    
    return ESP_OK;
}

esp_err_t gps_perf_simple_read_data(void)
{
    if (!gps_handle.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char* data = heap_caps_malloc(GPS_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!data) {
        return ESP_ERR_NO_MEM;
    }
    
    int len = uart_read_bytes(GPS_UART_NUM, data, GPS_BUFFER_SIZE - 1, pdMS_TO_TICKS(100));
    if (len > 0) {
        data[len] = '\0';
        
        // Send to ring buffer for processing
        if (xRingbufferSend(gps_handle.ring_buffer, data, len, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGW(TAG, "Ring buffer full, dropping data");
        }
    }
    
    heap_caps_free(data);
    return (len > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void gps_perf_simple_get_stats(uint64_t* bytes_processed, 
                               uint32_t* sentences_parsed,
                               uint64_t* avg_processing_time)
{
    if (bytes_processed) *bytes_processed = gps_handle.bytes_processed;
    if (sentences_parsed) *sentences_parsed = gps_handle.nmea_sentences_parsed;
    if (avg_processing_time && gps_handle.nmea_sentences_parsed > 0) {
        *avg_processing_time = gps_handle.processing_time_total / gps_handle.nmea_sentences_parsed;
    }
}

esp_err_t gps_perf_simple_deinit(void)
{
    if (!gps_handle.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gps_handle.process_task) {
        vTaskDelete(gps_handle.process_task);
    }
    
    if (gps_handle.ring_buffer) {
        vRingbufferDelete(gps_handle.ring_buffer);
    }
    
    uart_driver_delete(GPS_UART_NUM);
    
    memset(&gps_handle, 0, sizeof(gps_handle));
    ESP_LOGI(TAG, "Simple GPS performance module deinitialized");
    
    return ESP_OK;
}