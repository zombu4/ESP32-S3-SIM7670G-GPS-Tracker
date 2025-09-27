/**
 * ESP32-S3 GDMA Streaming Pipeline Engine
 * 
 * REVOLUTIONARY DMA SYSTEM: True streaming "pipes" with linked descriptor chains
 * that walk endlessly through buffers without CPU intervention!
 * 
 * Key Features:
 * - Linked-list descriptor chains for continuous streaming
 * - Double/triple buffering with automatic queue management
 * - Zero-copy DMA operations in optimized memory regions
 * - Producer-consumer pattern with FreeRTOS integration
 * - Perfect for LCD_CAM, I2S-LCD, SPI, UART continuous operations
 */

#ifndef ESP32S3_GDMA_STREAMING_H
#define ESP32S3_GDMA_STREAMING_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_private/gdma.h"    // ESP-IDF GDMA driver - CONFIRMED AVAILABLE!
#include "hal/gdma_hal.h"        // Hardware abstraction layer for GDMA
// GDMA driver CONFIRMED AVAILABLE in ESP-IDF v5.5 - using native implementation!

#ifdef __cplusplus
extern "C" {
#endif

// GDMA Streaming Configuration
#define GDMA_STREAM_MAX_BUFFERS     8     // Maximum streaming buffers
#define GDMA_STREAM_DEFAULT_CHUNK   4096  // Default chunk size (4KB)
#define GDMA_STREAM_QUEUE_SIZE      16    // Buffer queue size

// GDMA Stream Types
typedef enum {
    GDMA_STREAM_LCD_CAM_TX = 0,    // LCD_CAM transmit stream
    GDMA_STREAM_LCD_CAM_RX,        // LCD_CAM receive stream  
    GDMA_STREAM_I2S_LCD_TX,        // I2S-LCD transmit stream
    GDMA_STREAM_SPI_TX,            // SPI transmit stream
    GDMA_STREAM_SPI_RX,            // SPI receive stream
    GDMA_STREAM_UART_TX,           // UART transmit stream
    GDMA_STREAM_UART_RX,           // UART receive stream
    GDMA_STREAM_CUSTOM             // Custom stream type
} gdma_stream_type_t;

// GDMA Stream Handle
typedef struct gdma_stream_s* gdma_stream_handle_t;

// Buffer Status
typedef enum {
    GDMA_BUFFER_FREE = 0,          // Buffer is free for use
    GDMA_BUFFER_FILLING,           // Buffer is being filled by DMA
    GDMA_BUFFER_READY,             // Buffer is ready for processing
    GDMA_BUFFER_PROCESSING         // Buffer is being processed by consumer
} gdma_buffer_status_t;

// GDMA Stream Buffer
typedef struct {
    uint8_t* data;                 // Buffer data (DMA-capable memory)
    size_t size;                   // Buffer size in bytes
    size_t length;                 // Actual data length in buffer
    gdma_buffer_status_t status;   // Current buffer status
    uint64_t timestamp;            // Buffer timestamp
    void* user_data;               // User-defined data pointer
} gdma_stream_buffer_t;

// GDMA Stream Configuration
typedef struct {
    gdma_stream_type_t type;       // Stream type
    size_t buffer_size;            // Individual buffer size
    uint8_t buffer_count;          // Number of buffers (2-8)
    bool enable_timestamps;        // Enable buffer timestamping
    uint32_t queue_timeout_ms;     // Queue timeout for buffer operations
    TaskHandle_t consumer_task;    // Consumer task handle (optional)
} gdma_stream_config_t;

// Stream Callback Functions
typedef void (*gdma_stream_tx_done_cb_t)(gdma_stream_handle_t handle, gdma_stream_buffer_t* buffer, void* user_ctx);
typedef void (*gdma_stream_rx_done_cb_t)(gdma_stream_handle_t handle, gdma_stream_buffer_t* buffer, void* user_ctx);

// GDMA Stream Callbacks
typedef struct {
    gdma_stream_tx_done_cb_t tx_done;     // TX transfer complete callback
    gdma_stream_rx_done_cb_t rx_done;     // RX transfer complete callback
    void* user_ctx;                       // User context for callbacks
} gdma_stream_callbacks_t;

// GDMA Stream Performance Stats
typedef struct {
    uint64_t total_buffers_processed;     // Total buffers processed
    uint64_t total_bytes_transferred;     // Total bytes transferred
    uint32_t current_throughput_mbps;     // Current throughput in MB/s
    uint32_t peak_throughput_mbps;        // Peak throughput achieved
    uint32_t buffer_underruns;            // Number of buffer underruns
    uint32_t buffer_overruns;             // Number of buffer overruns
    float cpu_utilization_percent;        // CPU utilization for stream processing
} gdma_stream_stats_t;

/**
 * Initialize GDMA Streaming Pipeline
 * 
 * Creates the revolutionary continuous streaming system with linked descriptor
 * chains that operate without CPU intervention!
 * 
 * @param config Stream configuration
 * @param callbacks Stream callback functions
 * @param handle Output handle for GDMA stream
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_init(const gdma_stream_config_t* config, 
                          const gdma_stream_callbacks_t* callbacks,
                          gdma_stream_handle_t* handle);

/**
 * Setup Triple Buffer Pipeline
 * 
 * ZERO-LATENCY STREAMING: Creates the ultimate streaming pipeline where
 * DMA fills buffer A while CPU processes buffer B and buffer C is queued!
 * 
 * Perfect for:
 * - Continuous camera streaming to LCD
 * - Audio streaming pipelines
 * - High-speed sensor data acquisition
 * - Real-time signal processing
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_setup_triple_buffer(gdma_stream_handle_t handle);

/**
 * Start Continuous Streaming
 * 
 * INFINITE PIPELINE: Begins the linked descriptor chain that runs endlessly
 * without CPU intervention - the ultimate parallel processing unlock!
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_start(gdma_stream_handle_t handle);

/**
 * Get Next Buffer for Processing
 * 
 * PRODUCER-CONSUMER PATTERN: Get the next buffer filled by DMA for processing.
 * This function integrates seamlessly with FreeRTOS tasks.
 * 
 * @param handle GDMA stream handle
 * @param buffer Output buffer pointer
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t gdma_stream_get_buffer(gdma_stream_handle_t handle, 
                                gdma_stream_buffer_t** buffer, 
                                uint32_t timeout_ms);

/**
 * Return Processed Buffer
 * 
 * Returns a processed buffer back to the free pool for DMA to reuse.
 * Maintains the continuous streaming pipeline.
 * 
 * @param handle GDMA stream handle
 * @param buffer Buffer to return
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_return_buffer(gdma_stream_handle_t handle, 
                                   gdma_stream_buffer_t* buffer);

/**
 * Queue Buffer for Transmission
 * 
 * ZERO-COPY TX: Queue a buffer for DMA transmission without copying data.
 * Perfect for continuous output streaming.
 * 
 * @param handle GDMA stream handle
 * @param data Data pointer (must be DMA-capable)
 * @param length Data length in bytes
 * @param timeout_ms Queue timeout
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_queue_tx_buffer(gdma_stream_handle_t handle, 
                                     const uint8_t* data, 
                                     size_t length,
                                     uint32_t timeout_ms);

/**
 * Enable Fast Path Streaming Mode
 * 
 * MAXIMUM THROUGHPUT UNLOCK: Optimizes all settings for sustained
 * high-throughput streaming with minimal CPU overhead.
 * 
 * Features enabled:
 * - GDMA priority boost
 * - Cache optimization
 * - CPU frequency locks
 * - Interrupt priority optimization
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_enable_fast_path(gdma_stream_handle_t handle);

/**
 * SIMD Stream Processor
 * 
 * PARALLEL PROCESSING BEAST: Process streaming data using ESP32-S3's
 * packed SIMD instructions while DMA continues filling next buffers!
 * 
 * @param handle GDMA stream handle
 * @param processor_func User-defined SIMD processing function
 * @return ESP_OK on success
 */
typedef void (*gdma_simd_processor_t)(const uint8_t* input, uint8_t* output, size_t length);
esp_err_t gdma_stream_set_simd_processor(gdma_stream_handle_t handle, 
                                        gdma_simd_processor_t processor_func);

/**
 * Get Streaming Performance Statistics
 * 
 * @param handle GDMA stream handle
 * @param stats Output performance statistics
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_get_stats(gdma_stream_handle_t handle, gdma_stream_stats_t* stats);

/**
 * GDMA Streaming Demonstration
 * 
 * Showcases the revolutionary streaming capabilities:
 * 1. Continuous DMA operation with zero CPU intervention
 * 2. Triple buffering for zero-latency processing
 * 3. Producer-consumer pattern with FreeRTOS integration
 * 4. Real-time performance monitoring
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_run_demonstration(gdma_stream_handle_t handle);

/**
 * Stop Streaming and Cleanup
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_stop(gdma_stream_handle_t handle);

/**
 * Cleanup GDMA Stream
 * 
 * @param handle GDMA stream handle
 * @return ESP_OK on success
 */
esp_err_t gdma_stream_deinit(gdma_stream_handle_t handle);

// Default configuration macros
#define GDMA_STREAM_DEFAULT_CONFIG(stream_type) {       \
    .type = stream_type,                                \
    .buffer_size = GDMA_STREAM_DEFAULT_CHUNK,          \
    .buffer_count = 3,                                  \
    .enable_timestamps = true,                          \
    .queue_timeout_ms = 100,                            \
    .consumer_task = NULL                               \
}

// Triple buffer configuration (maximum performance)
#define GDMA_STREAM_TRIPLE_BUFFER_CONFIG(stream_type) { \
    .type = stream_type,                                \
    .buffer_size = GDMA_STREAM_DEFAULT_CHUNK,          \
    .buffer_count = 3,                                  \
    .enable_timestamps = true,                          \
    .queue_timeout_ms = 10,                             \
    .consumer_task = NULL                               \
}

// High-throughput configuration
#define GDMA_STREAM_HIGH_THROUGHPUT_CONFIG(stream_type) { \
    .type = stream_type,                                  \
    .buffer_size = 8192,                                  \
    .buffer_count = 4,                                    \
    .enable_timestamps = false,                           \
    .queue_timeout_ms = 5,                                \
    .consumer_task = NULL                                 \
}

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_GDMA_STREAMING_H