/**
 * @file cellular_performance.c
 * @brief High-performance cellular transmission using Core 1 parallel processing
 * 
 * Implementation of Core 1 cellular transmission pipeline with:
 * 1. DMA packet queuing for burst transmission
 * 2. Parallel processing while GPS runs on Core 0  
 * 3. Performance measurement and optimization
 */

#include "cellular_performance.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char* TAG = "CELLULAR_PERF";

// Core 1 task handle and queue
static TaskHandle_t cellular_task_handle = NULL;
static QueueHandle_t transmission_queue = NULL;

/**
 * @brief High-priority cellular transmission task (Core 1)
 * 
 * Dedicated to Core 1 for parallel processing while GPS runs on Core 0.
 * Handles DMA packet transmission with performance measurement.
 */
static void cellular_transmission_task(void* pvParameters)
{
    cellular_perf_handle_t* handle = (cellular_perf_handle_t*)pvParameters;
    cellular_perf_packet_t* packet;
    
    ESP_LOGI(TAG, "Cellular transmission task started on Core 1");
    
    while (1) {
        // Wait for packets to transmit
        if (xQueueReceive(transmission_queue, &packet, portMAX_DELAY)) {
            uint64_t start_time = esp_timer_get_time();
            
            // Simulate cellular transmission (replace with actual implementation)
            ESP_LOGI(TAG, "Transmitting packet: size=%zu bytes, type=%d", 
                     packet->data_size, packet->packet_type);
            
            // Performance measurement
            uint64_t transmission_time = esp_timer_get_time() - start_time;
            handle->stats.total_packets++;
            handle->stats.total_bytes += packet->data_size;
            handle->stats.total_transmission_time += transmission_time;
            
            // Calculate throughput
            if (handle->stats.total_packets > 0) {
                handle->stats.average_throughput = 
                    (handle->stats.total_bytes * 1000000ULL) / handle->stats.total_transmission_time;
            }
            
            // Free DMA packet buffer
            if (packet->data) {
                heap_caps_free(packet->data);
            }
            heap_caps_free(packet);
            
            ESP_LOGD(TAG, "Packet transmitted in %llu μs, throughput: %llu bytes/sec", 
                     transmission_time, handle->stats.average_throughput);
        }
    }
}

esp_err_t cellular_perf_init(cellular_perf_handle_t* handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(handle, 0, sizeof(cellular_perf_handle_t));
    
    // Create transmission queue (DMA-capable)
    transmission_queue = xQueueCreate(CELLULAR_PERF_QUEUE_SIZE, sizeof(cellular_perf_packet_t*));
    if (!transmission_queue) {
        ESP_LOGE(TAG, "Failed to create transmission queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Create high-priority task pinned to Core 1
    BaseType_t task_result = xTaskCreatePinnedToCore(
        cellular_transmission_task,
        "cellular_tx",
        CELLULAR_PERF_TASK_STACK_SIZE,
        handle,
        CELLULAR_PERF_TASK_PRIORITY,
        &cellular_task_handle,
        1  // Core 1
    );
    
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cellular transmission task");
        vQueueDelete(transmission_queue);
        return ESP_ERR_NO_MEM;
    }
    
    handle->initialized = true;
    ESP_LOGI(TAG, "Cellular performance module initialized on Core 1");
    
    return ESP_OK;
}

esp_err_t cellular_perf_transmit_packet(cellular_perf_handle_t* handle, 
                                        const uint8_t* data, 
                                        size_t data_size,
                                        cellular_packet_type_t type)
{
    if (!handle || !handle->initialized || !data || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate DMA-capable packet structure
    cellular_perf_packet_t* packet = heap_caps_malloc(sizeof(cellular_perf_packet_t), 
                                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!packet) {
        ESP_LOGE(TAG, "Failed to allocate packet structure");
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate DMA-capable data buffer
    packet->data = heap_caps_malloc(data_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!packet->data) {
        ESP_LOGE(TAG, "Failed to allocate packet data buffer");
        heap_caps_free(packet);
        return ESP_ERR_NO_MEM;
    }
    
    // Copy data and set packet parameters
    memcpy(packet->data, data, data_size);
    packet->data_size = data_size;
    packet->packet_type = type;
    packet->timestamp = esp_timer_get_time();
    
    // Queue for transmission on Core 1
    if (xQueueSend(transmission_queue, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue packet for transmission");
        heap_caps_free(packet->data);
        heap_caps_free(packet);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t cellular_perf_get_stats(cellular_perf_handle_t* handle, 
                                  cellular_perf_stats_t* stats)
{
    if (!handle || !handle->initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(stats, &handle->stats, sizeof(cellular_perf_stats_t));
    return ESP_OK;
}

esp_err_t cellular_perf_deinit(cellular_perf_handle_t* handle)
{
    if (!handle || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Delete task and queue
    if (cellular_task_handle) {
        vTaskDelete(cellular_task_handle);
        cellular_task_handle = NULL;
    }
    
    if (transmission_queue) {
        vQueueDelete(transmission_queue);
        transmission_queue = NULL;
    }
    
    handle->initialized = false;
    ESP_LOGI(TAG, "Cellular performance module deinitialized");
    
    return ESP_OK;
}