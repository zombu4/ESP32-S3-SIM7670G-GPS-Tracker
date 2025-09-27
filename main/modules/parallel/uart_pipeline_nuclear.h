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
#define GPS_RING_SIZE              (64 * 1024)  // 64KB for NMEA data buffering
#define GPS_NMEA_BUFFER_SIZE       (128 * 1024) // 128KB circular buffer for 30-second intervals
#define RING_BUFFER_TIMEOUT_MS     100

// GPS Polling Configuration - 30 Second Intervals
#define GPS_NMEA_POLL_INTERVAL_MS  (30 * 1000)  // 30 seconds
#define GPS_NMEA_BURST_DURATION_MS (2 * 1000)   // 2 seconds of NMEA collection per poll
#define GPS_POLLING_TASK_STACK_SIZE (8 * 1024)  // 8KB stack for GPS polling
#define GPS_POLLING_TASK_PRIORITY   (2)         // Lower priority than demux (3)

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

typedef enum {
    PIPELINE_ROUTE_CELLULAR = 0,
    PIPELINE_ROUTE_GPS,
    PIPELINE_ROUTE_SYSTEM,
    PIPELINE_ROUTE_COUNT
} pipeline_route_t;

typedef struct {
    pipeline_route_t route;
    uint32_t priority;
    bool active;
    SemaphoreHandle_t access_mutex;
    uint32_t packets_routed;
    uint32_t bytes_processed;
} pipeline_route_info_t;

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
    
    // GDMA Channel Handle
    gdma_channel_handle_t gdma_rx_channel;
    gdma_rx_event_callbacks_t gdma_callbacks;
    
    // Ring Buffers (Optimized for ESP32-S3)
    RingbufHandle_t cellular_ringbuf;
    RingbufHandle_t gps_ringbuf;
    
    // 🔥 NEW: GPS NMEA Circular Buffer for 30-second intervals
    uint8_t *gps_nmea_buffer;
    size_t gps_nmea_buffer_size;
    volatile size_t gps_nmea_write_pos;
    volatile size_t gps_nmea_read_pos;
    volatile bool gps_nmea_buffer_full;
    SemaphoreHandle_t gps_buffer_mutex;
    
    // 🔥 NEW: Pipeline Routing System
    pipeline_route_info_t routes[PIPELINE_ROUTE_COUNT];
    SemaphoreHandle_t routing_mutex;
    volatile pipeline_route_t active_route;
    
    // 🔥 NEW: GPS Polling Control
    volatile bool gps_polling_active;
    volatile uint32_t last_gps_poll_ms;
    TaskHandle_t gps_polling_task;
    
    // UART Event Management
    QueueHandle_t uart_event_queue;
    
    // Statistics & Performance Monitoring
    uint32_t total_bytes_processed;
    uint32_t cellular_packets;
    uint32_t gps_packets;
    uint32_t parse_errors;
    uint32_t dma_overruns;
    uint32_t route_switches;
    uint32_t buffer_overflows;
    
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

// 🔥 NEW: PIPELINE ROUTING API FUNCTIONS

/**
 * Set active pipeline route (CELLULAR, GPS, or SYSTEM)
 */
esp_err_t nuclear_pipeline_set_route(nuclear_uart_pipeline_t *pipeline, pipeline_route_t route);

/**
 * Get current active pipeline route
 */
pipeline_route_t nuclear_pipeline_get_active_route(nuclear_uart_pipeline_t *pipeline);

/**
 * Enable/disable GPS NMEA polling (30-second intervals)
 */
esp_err_t nuclear_pipeline_set_gps_polling(nuclear_uart_pipeline_t *pipeline, bool enable);

/**
 * Read GPS NMEA data from circular buffer (thread-safe)
 */
size_t nuclear_pipeline_read_gps_buffer(nuclear_uart_pipeline_t *pipeline, 
                                       uint8_t *output_buffer, 
                                       size_t max_size);

/**
 * Clear GPS NMEA circular buffer
 */
void nuclear_pipeline_clear_gps_buffer(nuclear_uart_pipeline_t *pipeline);

/**
 * Route-specific AT command execution with automatic route switching
 */
esp_err_t nuclear_pipeline_send_cellular_command(nuclear_uart_pipeline_t *pipeline,
                                               const char *command,
                                               char *response,
                                               size_t response_size,
                                               uint32_t timeout_ms);

/**
 * Get routing statistics
 */
void nuclear_pipeline_get_routing_stats(nuclear_uart_pipeline_t *pipeline,
                                      uint32_t *route_switches,
                                      uint32_t *buffer_overflows,
                                      uint32_t *gps_polls);

/**
 * Reset pipeline statistics
 */
void nuclear_pipeline_reset_stats(nuclear_uart_pipeline_t *pipeline);

// 🎯 INTERNAL FUNCTIONS (DO NOT CALL DIRECTLY)
// Note: These are declared as static in implementation file

void nuclear_uart_event_task(void *parameters);

// 💀 BEAST MODE: Performance Optimization Macros
#define NUCLEAR_CACHE_ALIGN(size)     (((size) + 31) & ~31)
#define NUCLEAR_IRAM_ATTR            __attribute__((section(".iram1")))
#define NUCLEAR_FORCE_INLINE         __attribute__((always_inline)) inline

// Global pipeline instance (singleton pattern for maximum performance)
extern nuclear_uart_pipeline_t *g_nuclear_pipeline;