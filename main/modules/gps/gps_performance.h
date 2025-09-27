/**
 * @file gps_performance.h
 * @brief High-performance GPS data collection with IRAM/DMA optimization
 * 
 * ESP32-S3 Performance Features:
 * - IRAM_ATTR functions for deterministic ISR timing
 * - DMA-capable buffers for zero-copy operations  
 * - Core pinning for jitter-free processing
 * - PM locks for sustained 240MHz performance
 */

#pragma once

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/uart.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Performance configuration
#define GPS_PERF_CORE_ID            0       // Core 0 for time-critical GPS
#define GPS_PERF_PRIORITY           23      // High priority (below WiFi/BT)
#define GPS_PERF_STACK_SIZE         8192    // Adequate stack for processing

// DMA buffer configuration  
#define GPS_DMA_BUFFER_SIZE         4096    // DMA-capable NMEA buffer
#define GPS_DMA_BUFFER_COUNT        3       // Triple buffering
#define GPS_RING_BUFFER_SIZE        16384   // ISR→Task ringbuffer

// UART configuration for maximum performance
#define GPS_UART_NUM                UART_NUM_1
#define GPS_UART_BAUD_RATE          115200
#define GPS_UART_TX_PIN             17
#define GPS_UART_RX_PIN             18
#define GPS_UART_RTS_PIN            UART_PIN_NO_CHANGE
#define GPS_UART_CTS_PIN            UART_PIN_NO_CHANGE

/**
 * @brief GPS performance statistics
 */
typedef struct {
    uint64_t isr_count;                 // Total ISR invocations
    uint64_t bytes_processed;           // Total bytes from GPS
    uint32_t parse_time_us;             // Last parse time (microseconds)
    uint32_t cpu_freq_mhz;              // Current CPU frequency
    uint32_t buffer_overruns;           // DMA buffer overrun count
    uint32_t sentences_parsed;          // Valid NMEA sentences parsed
    float    throughput_kbps;           // Current throughput (KB/s)
} gps_perf_stats_t;

/**
 * @brief DMA buffer descriptor for zero-copy operations
 */
typedef struct {
    uint8_t* data;                      // DMA-capable buffer pointer
    size_t   size;                      // Buffer size
    size_t   length;                    // Data length in buffer
    uint64_t timestamp;                 // Collection timestamp (us)
    bool     in_use;                    // Buffer usage flag
} gps_dma_buffer_t;

/**
 * @brief GPS performance handle
 */
typedef struct gps_perf_handle_s gps_perf_handle_t;

/**
 * @brief GPS data callback with performance metrics
 * 
 * @param buffer DMA buffer containing GPS data
 * @param stats Performance statistics
 * @param user_data User context pointer
 */
typedef void (*gps_perf_callback_t)(const gps_dma_buffer_t* buffer, 
                                   const gps_perf_stats_t* stats,
                                   void* user_data);

/**
 * @brief GPS performance configuration
 */
typedef struct {
    gps_perf_callback_t callback;       // Data ready callback
    void*               user_data;      // User context
    bool                enable_pm_lock; // Lock CPU at 240MHz
    bool                enable_stats;   // Collect performance stats
    uint32_t            update_rate_hz; // Target GPS update rate
} gps_perf_config_t;

/**
 * @brief Initialize high-performance GPS collection
 * 
 * Features enabled:
 * - IRAM-placed ISRs for <1μs response time
 * - DMA-capable triple buffering for zero-copy
 * - Core pinning to prevent jitter
 * - PM locks for sustained performance
 * 
 * @param config Performance configuration
 * @param handle Output handle pointer
 * @return ESP_OK on success
 */
esp_err_t gps_perf_init(const gps_perf_config_t* config, gps_perf_handle_t** handle);

/**
 * @brief Start high-performance GPS data collection
 * 
 * @param handle GPS performance handle
 * @return ESP_OK on success
 */
esp_err_t gps_perf_start(gps_perf_handle_t* handle);

/**
 * @brief Stop GPS data collection
 * 
 * @param handle GPS performance handle
 * @return ESP_OK on success
 */
esp_err_t gps_perf_stop(gps_perf_handle_t* handle);

/**
 * @brief Get current performance statistics
 * 
 * @param handle GPS performance handle
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t gps_perf_get_stats(gps_perf_handle_t* handle, gps_perf_stats_t* stats);

/**
 * @brief Deinitialize GPS performance module
 * 
 * @param handle GPS performance handle
 * @return ESP_OK on success
 */
esp_err_t gps_perf_deinit(gps_perf_handle_t* handle);

/**
 * @brief IRAM-placed ISR handler for deterministic timing
 * 
 * This function is placed in IRAM for guaranteed <1μs response time.
 * It performs minimal processing and queues data to DMA buffers.
 * 
 * @param arg User argument (handle pointer)
 */
void IRAM_ATTR gps_perf_uart_isr_handler(void* arg);

/**
 * @brief Get DMA buffer for zero-copy operations
 * 
 * Returns a DMA-capable buffer that can be used directly with
 * peripheral operations without additional copying.
 * 
 * @param handle GPS performance handle
 * @return DMA buffer pointer or NULL if none available
 */
gps_dma_buffer_t* gps_perf_get_dma_buffer(gps_perf_handle_t* handle);

/**
 * @brief Release DMA buffer back to pool
 * 
 * @param handle GPS performance handle
 * @param buffer Buffer to release
 * @return ESP_OK on success
 */
esp_err_t gps_perf_release_buffer(gps_perf_handle_t* handle, gps_dma_buffer_t* buffer);

/**
 * @brief Measure actual performance metrics
 * 
 * Uses esp_timer_get_time() and esp_clk_cpu_freq() to measure:
 * - CPU frequency validation
 * - Parse timing measurements  
 * - Throughput calculations
 * 
 * @param handle GPS performance handle
 */
void gps_perf_measure_timing(gps_perf_handle_t* handle);

#ifdef __cplusplus
}
#endif