/**
 * ESP32-S3 Hardcore Performance Optimization Arsenal
 * 
 * NUCLEAR-GRADE techniques that separate pros from beginners:
 * - Compile/link-time wins with aggressive optimization
 * - Strategic memory placement and capability-based allocation  
 * - Interrupt routing and power management locks
 * - DMA queue depth and ETM-based zero-ISR chaining
 * - SIMD exploitation with prefetching
 */

#ifndef ESP32S3_HARDCORE_OPTIMIZATION_H
#define ESP32S3_HARDCORE_OPTIMIZATION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gptimer.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Power Management Lock Handles for Critical Performance Windows
extern esp_pm_lock_handle_t cpu_max_lock;
extern esp_pm_lock_handle_t no_sleep_lock;

// Performance Configuration Constants
#define HARDCORE_CACHE_LINE_SIZE        32
#define HARDCORE_PREFETCH_DISTANCE      64
#define HARDCORE_DMA_QUEUE_DEPTH        4
#define HARDCORE_SPI_BURST_SIZE         1024

// Capability-based memory allocation macros
#define ALLOC_HOT_DATA(size)    heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
#define ALLOC_BULK_DATA(size)   heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
#define ALLOC_IRAM_CODE(size)   heap_caps_malloc(size, MALLOC_CAP_EXEC | MALLOC_CAP_INTERNAL)

/**
 * Initialize hardcore performance optimization system
 */
esp_err_t hardcore_perf_init(void);

/**
 * Acquire maximum performance locks (CPU freq + no sleep)
 */
void hardcore_perf_lock_acquire(void);

/**
 * Release performance locks (allow power management)
 */
void hardcore_perf_lock_release(void);

/**
 * IRAM-optimized hot loop with prefetching for streaming data
 * Perfect for camera/audio pipelines with predictable access patterns
 */
void IRAM_ATTR hardcore_streaming_kernel_u8(uint8_t* dst, const uint8_t* src, size_t n);

/**
 * SIMD-optimized parallel accumulation using ESP32-S3 packed math
 * Processes 4×8-bit values per cycle with saturation
 */
void IRAM_ATTR hardcore_simd_accumulate(uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count);

/**
 * High-performance SPI transaction queue with burst mode
 * Ensures SPI bus never idles between transfers
 */
esp_err_t hardcore_spi_burst_queue(spi_device_handle_t spi, uint8_t** buffers, size_t buffer_count, size_t bytes_per_buffer);

/**
 * Cache-aligned DMA buffer allocation
 * Prevents partial cache line churn for optimal DMA performance
 */
void* hardcore_alloc_dma_aligned(size_t size);

/**
 * Free cache-aligned DMA buffer
 */
void hardcore_free_dma_aligned(void* ptr);

/**
 * Timer-driven GDMA chain setup (ETM-based zero-ISR)
 * Timer event autonomously triggers next DMA segment
 */
esp_err_t hardcore_setup_timer_gdma_chain(gptimer_handle_t timer, uint64_t period_us);

/**
 * Measure and log performance statistics
 */
typedef struct {
    uint64_t start_time;
    uint64_t end_time;
    uint32_t operations;
    const char* label;
} hardcore_perf_stats_t;

void hardcore_perf_start(hardcore_perf_stats_t* stats, const char* label);
void hardcore_perf_end(hardcore_perf_stats_t* stats, uint32_t operations);
void hardcore_perf_report(const hardcore_perf_stats_t* stats);

/**
 * Advanced memory statistics with capability breakdown
 */
typedef struct {
    uint32_t internal_free;
    uint32_t internal_total;
    uint32_t psram_free;
    uint32_t psram_total;
    uint32_t dma_capable_free;
    uint32_t largest_block;
} hardcore_memory_stats_t;

void hardcore_get_memory_stats(hardcore_memory_stats_t* stats);
void hardcore_log_memory_stats(const hardcore_memory_stats_t* stats);

/**
 * Compiler optimization hints and intrinsics
 */
#define HARDCORE_LIKELY(x)      __builtin_expect(!!(x), 1)
#define HARDCORE_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define HARDCORE_PREFETCH_R(addr)  __builtin_prefetch((addr), 0, 3)  // Read, high locality
#define HARDCORE_PREFETCH_W(addr)  __builtin_prefetch((addr), 1, 3)  // Write, high locality

/**
 * Hot path function attributes
 */
#define HARDCORE_HOT_FUNC       __attribute__((hot)) IRAM_ATTR
#define HARDCORE_COLD_FUNC      __attribute__((cold))

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_HARDCORE_OPTIMIZATION_H