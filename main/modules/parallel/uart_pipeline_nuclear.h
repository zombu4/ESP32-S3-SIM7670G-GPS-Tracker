#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "esp_private/gdma.h"  // ESP-IDF v5.5 Private GDMA APIs
#include "hal/gdma_hal.h"
#include "hal/gdma_ll.h" 
#include "hal/gdma_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_etm.h"
#include "soc/uart_periph.h"    // UART peripheral definitions
#include "hal/uart_hal.h"       // UART HAL for low-level access
#include "esp_heap_caps.h"
#include "esp_cache.h"          // Cache management for DMA

// 💀🔥 NUCLEAR GDMA+ETM UART PIPELINE CONFIGURATION 🔥💀

#define NUCLEAR_UART_PORT           UART_NUM_1
#define NUCLEAR_UART_BAUD          115200
#define NUCLEAR_TX_PIN             18
#define NUCLEAR_RX_PIN             17

// DMA Configuration - MAXIMUM PERFORMANCE
#define GDMA_BUFFER_SIZE           4096    // 4KB per DMA descriptor
#define GDMA_DESCRIPTOR_COUNT      4       // Linked-list chain depth
#define GDMA_TOTAL_BUFFER_SIZE     (GDMA_BUFFER_SIZE * GDMA_DESCRIPTOR_COUNT)

// Ring Buffer Configuration - PSRAM Optimized
#define CELLULAR_RING_SIZE         (16 * 1024)  // 16KB for AT commands
#define GPS_RING_SIZE              (8 * 1024)   // 8KB for NMEA data
#define RING_BUFFER_TIMEOUT_MS     100

// ETM Event Matrix Configuration
#define ETM_UART_RX_EVENT          0
#define ETM_DMA_DONE_EVENT         1
#define ETM_PARSE_TRIGGER_TASK     0

// Stream Demux Magic Numbers
#define AT_COMMAND_PREFIX          "AT"
#define NMEA_SENTENCE_PREFIX       "$G"
#define SIM7670_RESPONSE_PREFIX    "+"

// 🚀 NUCLEAR PIPELINE STRUCTURES

typedef enum {
    STREAM_TYPE_UNKNOWN = 0,
    STREAM_TYPE_AT_CMD,
    STREAM_TYPE_AT_RESPONSE,  
    STREAM_TYPE_NMEA,
    STREAM_TYPE_ERROR
} nuclear_stream_type_t;

typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t write_pos;
    nuclear_stream_type_t stream_type;
    uint32_t timestamp_us;
} nuclear_dma_descriptor_t;

typedef struct {
    // ESP32-S3 Native Resources
    nuclear_dma_descriptor_t dma_descriptors[GDMA_DESCRIPTOR_COUNT];
    volatile uint32_t active_descriptor_index;
    
    // Ring Buffers (Optimized for ESP32-S3)
    RingbufHandle_t cellular_ringbuf;
    RingbufHandle_t gps_ringbuf;
    
    // UART Event Management
    QueueHandle_t uart_event_queue;
    
    // Statistics & Performance Monitoring
    uint32_t total_bytes_processed;
    uint32_t cellular_packets;
    uint32_t gps_packets;
    uint32_t parse_errors;
    uint32_t dma_overruns;
    
    // Control Flags & Task Handles
    volatile bool pipeline_active;
    volatile bool dma_running;
    TaskHandle_t demux_task_handle;
    TaskHandle_t event_task_handle;
} nuclear_uart_pipeline_t;

// 💀🔥 NUCLEAR API FUNCTIONS

/**
 * Initialize the nuclear GDMA+ETM UART pipeline
 */
esp_err_t nuclear_uart_pipeline_init(nuclear_uart_pipeline_t *pipeline);

/**
 * Start the nuclear processing pipeline
 */
esp_err_t nuclear_uart_pipeline_start(nuclear_uart_pipeline_t *pipeline);

/**
 * Stop the nuclear processing pipeline  
 */
esp_err_t nuclear_uart_pipeline_stop(nuclear_uart_pipeline_t *pipeline);

/**
 * Deinitialize and cleanup all resources
 */
esp_err_t nuclear_uart_pipeline_deinit(nuclear_uart_pipeline_t *pipeline);

/**
 * Read cellular AT command/response data (zero-copy)
 */
size_t nuclear_pipeline_read_cellular(nuclear_uart_pipeline_t *pipeline, 
                                    uint8_t **data_ptr, 
                                    TickType_t timeout_ticks);

/**
 * Read GPS NMEA data (zero-copy)  
 */
size_t nuclear_pipeline_read_gps(nuclear_uart_pipeline_t *pipeline,
                                uint8_t **data_ptr,
                                TickType_t timeout_ticks);

/**
 * Return ring buffer item after processing
 */
void nuclear_pipeline_return_buffer(nuclear_uart_pipeline_t *pipeline, uint8_t *data_ptr, bool is_gps);

/**
 * Return data buffer to ring buffer system
 */
void nuclear_pipeline_return_buffer(nuclear_uart_pipeline_t *pipeline,
                                  uint8_t *data_ptr,
                                  bool is_cellular);

/**
 * Send AT command through pipeline (thread-safe)
 */
esp_err_t nuclear_pipeline_send_at_command(nuclear_uart_pipeline_t *pipeline,
                                         const char *command,
                                         char *response,
                                         size_t response_size,
                                         uint32_t timeout_ms);

/**
 * Get pipeline statistics and performance metrics
 */
void nuclear_pipeline_get_stats(nuclear_uart_pipeline_t *pipeline,
                               uint32_t *total_bytes,
                               uint32_t *cellular_packets, 
                               uint32_t *gps_packets,
                               uint32_t *parse_errors);

/**
 * Reset pipeline statistics
 */
void nuclear_pipeline_reset_stats(nuclear_uart_pipeline_t *pipeline);

// 🎯 INTERNAL FUNCTIONS (DO NOT CALL DIRECTLY)

void nuclear_stream_demultiplexer_task(void *parameters);

void nuclear_uart_event_task(void *parameters);

nuclear_stream_type_t nuclear_detect_stream_type(const uint8_t *data, size_t len);

// 💀 BEAST MODE: Performance Optimization Macros
#define NUCLEAR_CACHE_ALIGN(size)     (((size) + 31) & ~31)
#define NUCLEAR_IRAM_ATTR            __attribute__((section(".iram1")))
#define NUCLEAR_FORCE_INLINE         __attribute__((always_inline)) inline

// Global pipeline instance (singleton pattern for maximum performance)
extern nuclear_uart_pipeline_t *g_nuclear_pipeline;