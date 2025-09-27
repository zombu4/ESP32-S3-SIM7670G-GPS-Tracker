/**
 * @file cellular_performance.h
 * @brief High-performance cellular transmission with DMA and Core 1
 * 
 * ESP32-S3 Performance Features for Network I/O:
 * - Core 1 pinning for I/O orchestration
 * - DMA-capable buffers for zero-copy network operations
 * - Parallel transmission pipeline while GPS processes on Core 0
 * - PM locks during transmission bursts
 */

#pragma once

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "gps_performance.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Performance configuration
#define CELLULAR_PERF_CORE_ID       1       // Core 1 for I/O orchestration
#define CELLULAR_PERF_PRIORITY      22      // High priority (below GPS)
#define CELLULAR_PERF_STACK_SIZE    8192    // Stack for network operations

// DMA buffer configuration
#define CELLULAR_DMA_BUFFER_SIZE    8192    // DMA-capable transmission buffer
#define CELLULAR_TX_QUEUE_SIZE      16      // Transmission queue depth

/**
 * @brief Cellular performance statistics
 */
typedef struct {
    uint64_t packets_sent;              // Total packets transmitted
    uint64_t bytes_sent;                // Total bytes transmitted
    uint32_t transmission_time_us;      // Last transmission time (μs)
    uint32_t queue_depth;               // Current queue depth
    uint32_t transmission_errors;       // Transmission error count
    float    throughput_kbps;           // Network throughput (KB/s)
    uint32_t cpu_freq_mhz;              // CPU frequency during transmission
} cellular_perf_stats_t;

/**
 * @brief DMA transmission packet
 */
typedef struct {
    uint8_t* data;                      // DMA-capable data buffer
    size_t   length;                    // Data length
    uint64_t timestamp;                 // Creation timestamp
    uint32_t priority;                  // Transmission priority
    void*    user_data;                 // User context
} cellular_dma_packet_t;

/**
 * @brief Cellular performance handle
 */
typedef struct cellular_perf_handle_s cellular_perf_handle_t;

/**
 * @brief Transmission completion callback
 * 
 * @param packet Transmitted packet
 * @param result Transmission result (ESP_OK on success)
 * @param stats Performance statistics
 * @param user_data User context
 */
typedef void (*cellular_perf_tx_callback_t)(const cellular_dma_packet_t* packet,
                                           esp_err_t result,
                                           const cellular_perf_stats_t* stats,
                                           void* user_data);

/**
 * @brief Cellular performance configuration
 */
typedef struct {
    cellular_perf_tx_callback_t tx_callback;    // Transmission callback
    void*                      user_data;      // User context
    bool                       enable_pm_lock; // Lock performance during TX
    bool                       enable_stats;   // Collect statistics
    uint32_t                   batch_size;     // Packets per transmission batch
} cellular_perf_config_t;

/**
 * @brief Initialize high-performance cellular transmission
 * 
 * Features:
 * - Core 1 pinning for parallel processing with GPS
 * - DMA-capable transmission buffers
 * - Batched transmission for efficiency
 * - Performance measurement and optimization
 * 
 * @param config Performance configuration
 * @param handle Output handle pointer
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_init(const cellular_perf_config_t* config, cellular_perf_handle_t** handle);

/**
 * @brief Start cellular transmission engine
 * 
 * @param handle Cellular performance handle
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_start(cellular_perf_handle_t* handle);

/**
 * @brief Stop cellular transmission engine
 * 
 * @param handle Cellular performance handle
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_stop(cellular_perf_handle_t* handle);

/**
 * @brief Queue GPS data for high-performance transmission
 * 
 * Creates DMA-capable packet from GPS buffer and queues for transmission.
 * Uses zero-copy approach when possible.
 * 
 * @param handle Cellular performance handle
 * @param gps_buffer GPS data buffer
 * @param priority Transmission priority (higher = sooner)
 * @param user_data User context for callback
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_queue_gps_data(cellular_perf_handle_t* handle,
                                      const gps_dma_buffer_t* gps_buffer,
                                      uint32_t priority,
                                      void* user_data);

/**
 * @brief Queue raw data for transmission
 * 
 * @param handle Cellular performance handle
 * @param data Data to transmit (will be copied to DMA buffer)
 * @param length Data length
 * @param priority Transmission priority
 * @param user_data User context for callback
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_queue_data(cellular_perf_handle_t* handle,
                                  const uint8_t* data,
                                  size_t length,
                                  uint32_t priority,
                                  void* user_data);

/**
 * @brief Get current performance statistics
 * 
 * @param handle Cellular performance handle
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_get_stats(cellular_perf_handle_t* handle, cellular_perf_stats_t* stats);

/**
 * @brief Force immediate transmission of queued data
 * 
 * Bypasses normal batching and transmits all queued packets immediately.
 * Useful for urgent data or shutdown scenarios.
 * 
 * @param handle Cellular performance handle
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_flush(cellular_perf_handle_t* handle);

/**
 * @brief Deinitialize cellular performance module
 * 
 * @param handle Cellular performance handle
 * @return ESP_OK on success
 */
esp_err_t cellular_perf_deinit(cellular_perf_handle_t* handle);

/**
 * @brief Measure transmission performance
 * 
 * Uses esp_timer_get_time() and esp_clk_cpu_freq() to measure:
 * - Transmission timing
 * - CPU frequency validation
 * - Throughput calculations
 * 
 * @param handle Cellular performance handle
 */
void cellular_perf_measure_timing(cellular_perf_handle_t* handle);

#ifdef __cplusplus
}
#endif