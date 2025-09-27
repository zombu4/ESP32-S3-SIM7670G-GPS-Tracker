/**
 * @file performance_demo.c
 * @brief Demonstration of ESP32-S3 high-performance optimizations
 * 
 * This file implements the performance guidelines:
 * 1. IRAM_ATTR functions for deterministic ISR timing
 * 2. DMA-capable buffers with heap_caps_malloc()
 * 3. Core pinning and task priorities
 * 4. Performance measurement with esp_timer_get_time()
 * 5. PM locks for sustained 240MHz CPU + 80MHz APB
 */

// #include "modules/gps/gps_performance.h" // Complex version disabled
esp_err_t gps_perf_simple_init(void);
esp_err_t gps_perf_simple_read_data(void);
void gps_perf_simple_get_stats(uint64_t* bytes_processed, uint32_t* sentences_parsed, uint64_t* avg_processing_time);
#include "cellular_performance.h"
#include "esp_log.h"
#include "esp_clk_tree.h"
#include "esp_pm.h"
#include <string.h>

static const char* TAG = "PERF_DEMO";

// Performance measurement variables
static uint64_t demo_start_time = 0;
static uint32_t demo_iteration_count = 0;

/**
 * @brief GPS data callback - processes GPS data on Core 0
 */
static void IRAM_ATTR gps_data_callback(const gps_dma_buffer_t* buffer, 
                                        const gps_perf_stats_t* stats,
                                        void* user_data)
{
    cellular_perf_handle_t* cellular_handle = (cellular_perf_handle_t*)user_data;
    
    if (!buffer || !buffer->data || buffer->length == 0) {
        return;
    }
    
    // Measure processing time (guideline #4)
    uint64_t start_time = esp_timer_get_time();
    
    // Queue GPS data for transmission on Core 1 (zero-copy when possible)
    esp_err_t ret = cellular_perf_queue_gps_data(cellular_handle, buffer, 1, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue GPS data: %s", esp_err_to_name(ret));
    }
    
    uint64_t end_time = esp_timer_get_time();
    uint32_t processing_time_us = (uint32_t)(end_time - start_time);
    
    // Log performance metrics (validate we're actually faster)
    ESP_LOGI(TAG, "📊 GPS→Cellular handoff: %d μs, Buffer: %zu bytes, Core: %d", 
             processing_time_us, buffer->length, xPortGetCoreID());
    
    demo_iteration_count++;
}

/**
 * @brief Cellular transmission callback - handles completion on Core 1
 */
static void cellular_tx_callback(const cellular_dma_packet_t* packet,
                                esp_err_t result,
                                const cellular_perf_stats_t* stats,
                                void* user_data)
{
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "✅ Transmitted %zu bytes, Throughput: %.2f KB/s, Core: %d", 
                 packet->length, stats->throughput_kbps, xPortGetCoreID());
    } else {
        ESP_LOGE(TAG, "❌ Transmission failed: %s", esp_err_to_name(result));
    }
}

/**
 * @brief Performance measurement task
 * 
 * Measures and validates performance according to guideline #4:
 * - Read back esp_clk_cpu_freq() 
 * - Time inner loops with esp_timer_get_time()
 * - Validate we're actually faster
 */
static void performance_measurement_task(void* pvParameters)
{
    ESP_LOGI(TAG, "📊 Performance measurement task started on core %d", xPortGetCoreID());
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Every 5 seconds
        
        // Measure CPU frequency (guideline #4)
        uint32_t cpu_freq_hz;
        esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX, &cpu_freq_hz);
        uint32_t cpu_freq_mhz = cpu_freq_hz / 1000000;
        
        // Calculate performance metrics
        uint64_t current_time = esp_timer_get_time();
        uint64_t elapsed_ms = (current_time - demo_start_time) / 1000;
        float iterations_per_second = (float)demo_iteration_count * 1000.0f / (float)elapsed_ms;
        
        ESP_LOGI(TAG, "🔥 Performance Report:");
        ESP_LOGI(TAG, "   CPU Frequency: %d MHz (Target: 240 MHz)", cpu_freq_mhz);
        ESP_LOGI(TAG, "   Iterations/sec: %.2f", iterations_per_second);
        ESP_LOGI(TAG, "   Total iterations: %d", demo_iteration_count);
        ESP_LOGI(TAG, "   Runtime: %llu ms", elapsed_ms);
        
        // Validate performance expectations
        if (cpu_freq_mhz >= 240) {
            ESP_LOGI(TAG, "✅ CPU running at maximum frequency");
        } else {
            ESP_LOGW(TAG, "⚠️  CPU frequency below maximum: %d MHz", cpu_freq_mhz);
        }
        
        if (iterations_per_second > 1.0f) {
            ESP_LOGI(TAG, "✅ Good processing throughput");
        } else {
            ESP_LOGW(TAG, "⚠️  Low processing throughput: %.2f/sec", iterations_per_second);
        }
    }
}

/**
 * @brief Initialize and run high-performance GPS tracker demo
 * 
 * Demonstrates all optimization guidelines:
 * 1. IRAM functions for deterministic timing
 * 2. DMA-capable buffers 
 * 3. Core pinning and priorities
 * 4. Performance measurement
 */
esp_err_t performance_demo_init(void)
{
    ESP_LOGI(TAG, "🚀 Initializing ESP32-S3 High-Performance Demo");
    
    demo_start_time = esp_timer_get_time();
    demo_iteration_count = 0;
    
    // Initialize cellular performance module (Core 1)
    cellular_perf_config_t cellular_config = {
        .tx_callback = cellular_tx_callback,
        .user_data = NULL,
        .enable_pm_lock = true,  // Lock performance during transmission
        .enable_stats = true,
        .batch_size = 1
    };
    
    cellular_perf_handle_t* cellular_handle = NULL;
    esp_err_t ret = cellular_perf_init(&cellular_config, &cellular_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize cellular performance: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize GPS performance module (Core 0)
    gps_perf_config_t gps_config = {
        .callback = gps_data_callback,
        .user_data = cellular_handle,  // Pass cellular handle to GPS callback
        .enable_pm_lock = true,        // Lock CPU at 240MHz, APB at 80MHz
        .enable_stats = true,
        .update_rate_hz = 10          // 10Hz GPS updates
    };
    
    gps_perf_handle_t* gps_handle = NULL;
    ret = gps_perf_init(&gps_config, &gps_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GPS performance: %s", esp_err_to_name(ret));
        cellular_perf_deinit(cellular_handle);
        return ret;
    }
    
    // Start performance modules
    ret = cellular_perf_start(cellular_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start cellular performance: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gps_perf_start(gps_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start GPS performance: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create performance measurement task (any core)
    xTaskCreate(performance_measurement_task, "perf_measure", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "✅ High-performance demo initialized");
    ESP_LOGI(TAG, "🔥 GPS processing on Core 0, Cellular on Core 1");
    ESP_LOGI(TAG, "⚡ CPU locked at 240MHz, APB at 80MHz");
    ESP_LOGI(TAG, "📊 Performance measurement enabled");
    
    return ESP_OK;
}

/**
 * @brief Demonstrate IRAM function placement
 * 
 * This function shows how to place time-critical code in IRAM
 * for deterministic execution (guideline #2).
 */
void IRAM_ATTR demo_iram_function(void)
{
    // This function will be placed in IRAM for fast execution
    // Use for ISR handlers and other time-critical code
    
    uint64_t start_time = esp_timer_get_time();
    
    // Simulate time-critical processing
    volatile uint32_t dummy = 0;
    for (int i = 0; i < 1000; i++) {
        dummy += i;
    }
    
    uint64_t end_time = esp_timer_get_time();
    uint32_t execution_time = (uint32_t)(end_time - start_time);
    
    // This should show consistent, low execution time due to IRAM placement
    ESP_LOGI(TAG, "🎯 IRAM function executed in %d μs", execution_time);
}

/**
 * @brief Demonstrate DMA buffer allocation
 * 
 * Shows proper DMA-capable buffer allocation (guideline #2).
 */
void demo_dma_buffers(void)
{
    ESP_LOGI(TAG, "📦 Demonstrating DMA buffer allocation");
    
    // Allocate DMA-capable buffer (guideline #2)
    size_t buffer_size = 4096;
    uint8_t* dma_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    if (dma_buffer) {
        ESP_LOGI(TAG, "✅ DMA buffer allocated: %p, size: %zu", dma_buffer, buffer_size);
        
        // Fill buffer with test pattern
        for (size_t i = 0; i < buffer_size; i++) {
            dma_buffer[i] = (uint8_t)(i & 0xFF);
        }
        
        ESP_LOGI(TAG, "📝 DMA buffer filled with test pattern");
        
        // Free buffer
        heap_caps_free(dma_buffer);
        ESP_LOGI(TAG, "🗑️ DMA buffer freed");
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate DMA buffer");
    }
}