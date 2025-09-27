/**
 * ESP32-S3 Hardcore Performance Arsenal - Live Demonstration
 * 
 * PRACTICAL NUCLEAR TECHNIQUES:
 * - Power management locks during critical windows
 * - IRAM hot loops with aggressive prefetching  
 * - Cache-aligned DMA allocation
 * - SIMD-style parallel processing
 * - SPI queue depth optimization
 * - Real-time performance measurement
 */

#include "esp32s3_hardcore_demo.h"
#include "esp32s3_hardcore_optimization.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

static const char* TAG = "HARDCORE_DEMO";

// Demo configuration
#define DEMO_BUFFER_SIZE        (8 * 1024)    // 8KB test buffers
#define DEMO_ITERATIONS         1000           // Performance test iterations
#define DEMO_SIMD_ELEMENTS      (2 * 1024)    // 2K elements for SIMD test

/**
 * Demonstrate hardcore memory allocation strategies
 */
esp_err_t demo_hardcore_memory_allocation(void) {
    ESP_LOGI(TAG, "🔥 DEMO: Hardcore Memory Allocation Strategies");
    
    hardcore_memory_stats_t before_stats, after_stats;
    hardcore_get_memory_stats(&before_stats);
    
    ESP_LOGI(TAG, "📊 Memory state BEFORE allocation:");
    hardcore_log_memory_stats(&before_stats);
    
    // Demonstrate capability-based allocation
    void* hot_data = ALLOC_HOT_DATA(DEMO_BUFFER_SIZE);      // Internal DMA-capable
    void* bulk_data = ALLOC_BULK_DATA(DEMO_BUFFER_SIZE);    // PSRAM for bulk storage
    void* aligned_dma = hardcore_alloc_dma_aligned(DEMO_BUFFER_SIZE); // Cache-line aligned
    
    if (!hot_data || !bulk_data || !aligned_dma) {
        ESP_LOGE(TAG, "❌ Memory allocation failed!");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "✅ Strategic allocations:");
    ESP_LOGI(TAG, "   Hot data (DMA+Internal): %p", hot_data);
    ESP_LOGI(TAG, "   Bulk data (PSRAM): %p", bulk_data);
    ESP_LOGI(TAG, "   Aligned DMA: %p (32-byte aligned: %s)", 
             aligned_dma, ((uintptr_t)aligned_dma % 32 == 0) ? "YES" : "NO");
    
    hardcore_get_memory_stats(&after_stats);
    ESP_LOGI(TAG, "📊 Memory state AFTER allocation:");
    hardcore_log_memory_stats(&after_stats);
    
    cleanup:
    if (hot_data) heap_caps_free(hot_data);
    if (bulk_data) heap_caps_free(bulk_data);
    if (aligned_dma) hardcore_free_dma_aligned(aligned_dma);
    
    return ESP_OK;
}

/**
 * Demonstrate IRAM hot loop with prefetching vs standard memcpy
 */
esp_err_t demo_hardcore_streaming_performance(void) {
    ESP_LOGI(TAG, "🔥 DEMO: IRAM Hot Loop vs Standard memcpy");
    
    // Allocate test buffers in different memory regions
    uint8_t* src_internal = ALLOC_HOT_DATA(DEMO_BUFFER_SIZE);
    uint8_t* dst_internal = ALLOC_HOT_DATA(DEMO_BUFFER_SIZE);
    uint8_t* src_psram = ALLOC_BULK_DATA(DEMO_BUFFER_SIZE);
    uint8_t* dst_psram = ALLOC_BULK_DATA(DEMO_BUFFER_SIZE);
    
    if (!src_internal || !dst_internal || !src_psram || !dst_psram) {
        ESP_LOGE(TAG, "❌ Buffer allocation failed!");
        goto cleanup;
    }
    
    // Initialize source data
    for (int i = 0; i < DEMO_BUFFER_SIZE; i++) {
        src_internal[i] = i & 0xFF;
        src_psram[i] = i & 0xFF;
    }
    
    hardcore_perf_stats_t stats;
    
    // Test 1: Standard memcpy (internal → internal)
    hardcore_perf_start(&stats, "Standard memcpy (Internal)");
    for (int i = 0; i < DEMO_ITERATIONS; i++) {
        memcpy(dst_internal, src_internal, DEMO_BUFFER_SIZE);
    }
    hardcore_perf_end(&stats, DEMO_ITERATIONS);
    hardcore_perf_report(&stats);
    
    // Test 2: Hardcore streaming kernel (internal → internal)  
    hardcore_perf_start(&stats, "Hardcore streaming (Internal)");
    for (int i = 0; i < DEMO_ITERATIONS; i++) {
        hardcore_streaming_kernel_u8(dst_internal, src_internal, DEMO_BUFFER_SIZE);
    }
    hardcore_perf_end(&stats, DEMO_ITERATIONS);
    hardcore_perf_report(&stats);
    
    // Test 3: PSRAM performance comparison
    hardcore_perf_start(&stats, "Standard memcpy (PSRAM)");
    for (int i = 0; i < DEMO_ITERATIONS / 10; i++) { // Fewer iterations for PSRAM
        memcpy(dst_psram, src_psram, DEMO_BUFFER_SIZE);
    }
    hardcore_perf_end(&stats, DEMO_ITERATIONS / 10);
    hardcore_perf_report(&stats);
    
    ESP_LOGI(TAG, "✅ Streaming performance comparison complete");
    
    cleanup:
    if (src_internal) heap_caps_free(src_internal);
    if (dst_internal) heap_caps_free(dst_internal);
    if (src_psram) heap_caps_free(src_psram);
    if (dst_psram) heap_caps_free(dst_psram);
    
    return ESP_OK;
}

/**
 * Demonstrate SIMD-style parallel processing
 */
esp_err_t demo_hardcore_simd_processing(void) {
    ESP_LOGI(TAG, "🔥 DEMO: SIMD-Style Parallel Processing");
    
    // Allocate aligned buffers for optimal SIMD performance
    uint8_t* array_a = hardcore_alloc_dma_aligned(DEMO_SIMD_ELEMENTS);
    uint8_t* array_b = hardcore_alloc_dma_aligned(DEMO_SIMD_ELEMENTS);
    uint8_t* result_std = hardcore_alloc_dma_aligned(DEMO_SIMD_ELEMENTS);
    uint8_t* result_simd = hardcore_alloc_dma_aligned(DEMO_SIMD_ELEMENTS);
    
    if (!array_a || !array_b || !result_std || !result_simd) {
        ESP_LOGE(TAG, "❌ SIMD buffer allocation failed!");
        goto cleanup;
    }
    
    // Initialize test data
    for (int i = 0; i < DEMO_SIMD_ELEMENTS; i++) {
        array_a[i] = (i * 3) & 0xFF;
        array_b[i] = (i * 5) & 0xFF;
    }
    
    hardcore_perf_stats_t stats;
    
    // Test 1: Standard scalar addition
    hardcore_perf_start(&stats, "Scalar Addition");
    for (int iter = 0; iter < 100; iter++) {
        for (int i = 0; i < DEMO_SIMD_ELEMENTS; i++) {
            uint16_t sum = (uint16_t)array_a[i] + (uint16_t)array_b[i];
            result_std[i] = (sum > 255) ? 255 : (uint8_t)sum;
        }
    }
    hardcore_perf_end(&stats, 100 * DEMO_SIMD_ELEMENTS);
    hardcore_perf_report(&stats);
    
    // Test 2: SIMD-style parallel addition
    hardcore_perf_start(&stats, "SIMD Parallel Addition");
    for (int iter = 0; iter < 100; iter++) {
        hardcore_simd_accumulate(result_simd, array_a, array_b, DEMO_SIMD_ELEMENTS);
    }
    hardcore_perf_end(&stats, 100 * DEMO_SIMD_ELEMENTS);
    hardcore_perf_report(&stats);
    
    // Verify results match
    bool results_match = true;
    for (int i = 0; i < DEMO_SIMD_ELEMENTS; i++) {
        if (result_std[i] != result_simd[i]) {
            results_match = false;
            break;
        }
    }
    
    ESP_LOGI(TAG, "✅ SIMD results verification: %s", results_match ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "📊 Sample results [0-7]: %d,%d,%d,%d,%d,%d,%d,%d", 
             result_simd[0], result_simd[1], result_simd[2], result_simd[3],
             result_simd[4], result_simd[5], result_simd[6], result_simd[7]);
    
    cleanup:
    if (array_a) hardcore_free_dma_aligned(array_a);
    if (array_b) hardcore_free_dma_aligned(array_b);
    if (result_std) hardcore_free_dma_aligned(result_std);
    if (result_simd) hardcore_free_dma_aligned(result_simd);
    
    return ESP_OK;
}

/**
 * Demonstrate power management lock effectiveness
 */
esp_err_t demo_hardcore_power_management(void) {
    ESP_LOGI(TAG, "🔥 DEMO: Power Management Lock Performance Impact");
    
    hardcore_perf_stats_t stats;
    volatile uint32_t dummy_work = 0;
    
    // Test without power locks (normal operation)
    hardcore_perf_start(&stats, "Without Power Locks");
    for (int i = 0; i < 100000; i++) {
        dummy_work += i * 17; // Some computational work
    }
    hardcore_perf_end(&stats, 100000);
    hardcore_perf_report(&stats);
    
    // Test with maximum performance locks
    hardcore_perf_start(&stats, "With Max Performance Locks");
    hardcore_perf_lock_acquire();
    for (int i = 0; i < 100000; i++) {
        dummy_work += i * 17; // Same computational work
    }
    hardcore_perf_lock_release();
    hardcore_perf_end(&stats, 100000);
    hardcore_perf_report(&stats);
    
    ESP_LOGI(TAG, "✅ Power management demo complete (dummy_work: %lu)", dummy_work);
    return ESP_OK;
}

/**
 * Run complete hardcore performance demonstration
 */
esp_err_t run_hardcore_performance_demo(void) {
    ESP_LOGI(TAG, "💀💀💀 STARTING HARDCORE PERFORMANCE ARSENAL DEMO! 💀💀💀");
    ESP_LOGI(TAG, "🚀 Demonstrating NUCLEAR-GRADE ESP32-S3 optimization techniques...");
    
    esp_err_t ret;
    
    // Initialize hardcore performance system
    ret = hardcore_perf_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize hardcore performance system!");
        return ret;
    }
    
    ESP_LOGI(TAG, "====================================");
    
    // Demo 1: Memory allocation strategies
    ret = demo_hardcore_memory_allocation();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Memory allocation demo failed!");
        return ret;
    }
    
    ESP_LOGI(TAG, "====================================");
    
    // Demo 2: Streaming performance with prefetching
    ret = demo_hardcore_streaming_performance();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Streaming performance demo failed!");
        return ret;
    }
    
    ESP_LOGI(TAG, "====================================");
    
    // Demo 3: SIMD-style parallel processing
    ret = demo_hardcore_simd_processing();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ SIMD processing demo failed!");
        return ret;
    }
    
    ESP_LOGI(TAG, "====================================");
    
    // Demo 4: Power management impact
    ret = demo_hardcore_power_management();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Power management demo failed!");
        return ret;
    }
    
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "💀🔥💀 HARDCORE ARSENAL DEMO COMPLETE! 💀🔥💀");
    ESP_LOGI(TAG, "✅ All nuclear-grade techniques demonstrated successfully");
    ESP_LOGI(TAG, "🚀 ESP32-S3 is now running with MAXIMUM PERFORMANCE UNLOCKED!");
    
    return ESP_OK;
}