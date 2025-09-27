/**
 * ESP32-S3 Hardcore Performance Optimization Implementation
 * 
 * The NUCLEAR ARSENAL - Real-world performance techniques that
 * squeeze every drop of speed and determinism from ESP32-S3
 */

#include "esp32s3_hardcore_optimization.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const char* TAG = "HARDCORE_PERF";

// Global performance lock handles
esp_pm_lock_handle_t cpu_max_lock = NULL;
esp_pm_lock_handle_t no_sleep_lock = NULL;

/**
 * Initialize hardcore performance optimization system
 */
esp_err_t hardcore_perf_init(void) {
    ESP_LOGI(TAG, "🔥 Initializing HARDCORE Performance Arsenal...");
    
    esp_err_t ret;
    
    // Create CPU frequency lock (keep at max)
    ret = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "hardcore_cpu", &cpu_max_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CPU max lock: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create no-sleep lock (prevent light sleep during critical windows)
    ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "hardcore_awake", &no_sleep_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create no-sleep lock: %s", esp_err_to_name(ret));
        esp_pm_lock_delete(cpu_max_lock);
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Power management locks created successfully");
    ESP_LOGI(TAG, "💀 HARDCORE Performance Arsenal ARMED!");
    
    return ESP_OK;
}

/**
 * Acquire maximum performance locks
 */
void hardcore_perf_lock_acquire(void) {
    if (cpu_max_lock) esp_pm_lock_acquire(cpu_max_lock);
    if (no_sleep_lock) esp_pm_lock_acquire(no_sleep_lock);
}

/**
 * Release performance locks
 */
void hardcore_perf_lock_release(void) {
    if (no_sleep_lock) esp_pm_lock_release(no_sleep_lock);
    if (cpu_max_lock) esp_pm_lock_release(cpu_max_lock);
}

/**
 * IRAM-optimized streaming kernel with aggressive prefetching
 * NUCLEAR-GRADE technique: Cache-aware processing with look-ahead
 */
void HARDCORE_HOT_FUNC hardcore_streaming_kernel_u8(uint8_t* dst, const uint8_t* src, size_t n) {
    // Process in cache-line-sized chunks with prefetching
    for (size_t i = 0; i < n; i += HARDCORE_CACHE_LINE_SIZE) {
        // Aggressive prefetching - read/write ahead
        if (HARDCORE_LIKELY(i + HARDCORE_PREFETCH_DISTANCE < n)) {
            HARDCORE_PREFETCH_R(src + i + HARDCORE_PREFETCH_DISTANCE);
            HARDCORE_PREFETCH_W(dst + i + HARDCORE_PREFETCH_DISTANCE);
        }
        
        // Process current cache line (32 bytes)
        size_t chunk_size = (n - i < HARDCORE_CACHE_LINE_SIZE) ? (n - i) : HARDCORE_CACHE_LINE_SIZE;
        
        // Unrolled copy for maximum throughput
        for (size_t j = 0; j < chunk_size; j += 4) {
            // Process 4 bytes at once when possible
            if (j + 4 <= chunk_size) {
                uint32_t* src32 = (uint32_t*)(src + i + j);
                uint32_t* dst32 = (uint32_t*)(dst + i + j);
                *dst32 = *src32;
            } else {
                // Handle remaining bytes
                for (size_t k = j; k < chunk_size; k++) {
                    dst[i + k] = src[i + k];
                }
                break;
            }
        }
    }
}

/**
 * SIMD-optimized parallel accumulation (concept for ESP32-S3)
 * Real implementation would use Xtensa LX7 packed instructions
 */
void HARDCORE_HOT_FUNC hardcore_simd_accumulate(uint8_t* result, const uint8_t* a, const uint8_t* b, size_t count) {
    hardcore_perf_lock_acquire();
    
    // Process 4 elements at a time (SIMD simulation)
    size_t simd_count = count & ~3; // Round down to multiple of 4
    
    for (size_t i = 0; i < simd_count; i += 4) {
        // Prefetch next batch
        if (HARDCORE_LIKELY(i + 8 < simd_count)) {
            HARDCORE_PREFETCH_R(a + i + 8);
            HARDCORE_PREFETCH_R(b + i + 8);
            HARDCORE_PREFETCH_W(result + i + 8);
        }
        
        // Simulate 4×8-bit parallel add with saturation
        for (int j = 0; j < 4; j++) {
            uint16_t sum = (uint16_t)a[i + j] + (uint16_t)b[i + j];
            result[i + j] = (sum > 255) ? 255 : (uint8_t)sum; // Saturating add
        }
    }
    
    // Handle remaining elements
    for (size_t i = simd_count; i < count; i++) {
        uint16_t sum = (uint16_t)a[i] + (uint16_t)b[i];
        result[i] = (sum > 255) ? 255 : (uint8_t)sum;
    }
    
    hardcore_perf_lock_release();
}

/**
 * High-performance SPI burst with queue depth
 * NUCLEAR technique: Bus never idles, continuous streaming
 */
esp_err_t hardcore_spi_burst_queue(spi_device_handle_t spi, uint8_t** buffers, 
                                   size_t buffer_count, size_t bytes_per_buffer) {
    if (!spi || !buffers || buffer_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🚀 Starting SPI burst queue: %zu buffers × %zu bytes", buffer_count, bytes_per_buffer);
    
    spi_transaction_t transactions[HARDCORE_DMA_QUEUE_DEPTH];
    
    hardcore_perf_lock_acquire();
    
    // Initialize transaction queue
    for (int i = 0; i < HARDCORE_DMA_QUEUE_DEPTH && i < buffer_count; i++) {
        transactions[i].length = bytes_per_buffer * 8; // bits
        transactions[i].tx_buffer = buffers[i];
        transactions[i].rx_buffer = NULL;
        transactions[i].flags = 0;
        
        esp_err_t ret = spi_device_queue_trans(spi, &transactions[i], 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to queue initial SPI transaction %d", i);
            hardcore_perf_lock_release();
            return ret;
        }
    }
    
    // Process remaining buffers with continuous queueing
    for (size_t i = HARDCORE_DMA_QUEUE_DEPTH; i < buffer_count; i++) {
        spi_transaction_t* completed_trans;
        
        // Wait for completion and get the result
        esp_err_t ret = spi_device_get_trans_result(spi, &completed_trans, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transaction failed: %s", esp_err_to_name(ret));
            hardcore_perf_lock_release();
            return ret;
        }
        
        // Reuse the completed transaction for next buffer
        completed_trans->tx_buffer = buffers[i];
        
        // Immediately queue the next transaction
        ret = spi_device_queue_trans(spi, completed_trans, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to requeue SPI transaction");
            hardcore_perf_lock_release();
            return ret;
        }
    }
    
    // Wait for final transactions to complete
    for (int i = 0; i < HARDCORE_DMA_QUEUE_DEPTH && i < buffer_count; i++) {
        spi_transaction_t* completed_trans;
        spi_device_get_trans_result(spi, &completed_trans, portMAX_DELAY);
    }
    
    hardcore_perf_lock_release();
    
    ESP_LOGI(TAG, "✅ SPI burst queue completed: %zu buffers processed", buffer_count);
    return ESP_OK;
}

/**
 * Cache-aligned DMA buffer allocation
 */
void* hardcore_alloc_dma_aligned(size_t size) {
    // Round up to cache line boundary
    size_t aligned_size = (size + HARDCORE_CACHE_LINE_SIZE - 1) & ~(HARDCORE_CACHE_LINE_SIZE - 1);
    
    void* ptr = heap_caps_aligned_alloc(HARDCORE_CACHE_LINE_SIZE, aligned_size, 
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (ptr) {
        ESP_LOGD(TAG, "🎯 Allocated DMA buffer: %zu bytes (aligned to %d)", aligned_size, HARDCORE_CACHE_LINE_SIZE);
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate DMA-aligned buffer: %zu bytes", aligned_size);
    }
    
    return ptr;
}

/**
 * Free cache-aligned DMA buffer
 */
void hardcore_free_dma_aligned(void* ptr) {
    if (ptr) {
        heap_caps_free(ptr);
    }
}

/**
 * Performance measurement utilities
 */
void hardcore_perf_start(hardcore_perf_stats_t* stats, const char* label) {
    if (!stats) return;
    
    stats->label = label;
    stats->operations = 0;
    stats->start_time = esp_timer_get_time();
}

void hardcore_perf_end(hardcore_perf_stats_t* stats, uint32_t operations) {
    if (!stats) return;
    
    stats->end_time = esp_timer_get_time();
    stats->operations = operations;
}

void hardcore_perf_report(const hardcore_perf_stats_t* stats) {
    if (!stats || stats->end_time <= stats->start_time) return;
    
    uint64_t duration_us = stats->end_time - stats->start_time;
    double duration_ms = duration_us / 1000.0;
    double ops_per_sec = (stats->operations * 1000000.0) / duration_us;
    
    ESP_LOGI(TAG, "⚡ PERF [%s]: %lu ops in %.3f ms (%.2f ops/sec)", 
             stats->label ? stats->label : "Unknown", 
             stats->operations, duration_ms, ops_per_sec);
}

/**
 * Advanced memory statistics
 */
void hardcore_get_memory_stats(hardcore_memory_stats_t* stats) {
    if (!stats) return;
    
    // Internal memory
    stats->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    stats->internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    
    // PSRAM
    stats->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    stats->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    // DMA-capable memory
    stats->dma_capable_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    
    // Largest contiguous block
    stats->largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

void hardcore_log_memory_stats(const hardcore_memory_stats_t* stats) {
    if (!stats) return;
    
    ESP_LOGI(TAG, "💾 MEMORY STATS:");
    ESP_LOGI(TAG, "   Internal: %lu/%lu KB (%.1f%% used)", 
             (stats->internal_total - stats->internal_free) / 1024,
             stats->internal_total / 1024,
             100.0 * (stats->internal_total - stats->internal_free) / stats->internal_total);
             
    ESP_LOGI(TAG, "   PSRAM: %lu/%lu KB (%.1f%% used)",
             (stats->psram_total - stats->psram_free) / 1024,
             stats->psram_total / 1024, 
             stats->psram_total > 0 ? 100.0 * (stats->psram_total - stats->psram_free) / stats->psram_total : 0.0);
             
    ESP_LOGI(TAG, "   DMA-capable: %lu KB, Largest block: %lu KB",
             stats->dma_capable_free / 1024, stats->largest_block / 1024);
}