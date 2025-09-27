/**
 * @file esp32s3_ultra_parallel.h
 * @brief ESP32-S3 Ultra-Parallel Processing Architecture - THE COMPLETE UNLOCK!
 * 
 * REVOLUTIONARY DISCOVERY: ESP32-S3's TRUE parallel processing capabilities:
 * 1. EVENT TASK MATRIX (ETM): Peripheral-to-peripheral communication WITHOUT CPU!
 * 2. GDMA STREAMING PIPELINES: Linked-list descriptors for endless streaming  
 * 3. PACKED SIMD INSTRUCTIONS: 4×8-bit and 2×16-bit lane parallel processing
 * 4. ESP-DSP HARDWARE ACCELERATION: FIR, FFT, convolution with zero CPU overhead
 * 5. 32-PIN ATOMIC GPIO: Simultaneous multi-pin control in single instruction
 * 6. RMT MINI-PIO: Custom waveform generation with DMA streaming
 * 7. MCPWM PRECISION: Phase-aligned clocks with capture and dead-time control
 * 8. CAPABILITY-BASED MEMORY: Optimal allocation for IRAM/DMA/SPIRAM regions
 * 9. ULP RISC-V COPROCESSOR: Always-on parallel sensing independent of main cores
 * 10. POWER MANAGEMENT LOCKS: Deterministic timing with 240MHz frequency locking
 * 
 * This is THE ULTIMATE ESP32-S3 PARALLEL PROCESSING BEAST MODE!
 */

#ifndef ESP32S3_ULTRA_PARALLEL_H
#define ESP32S3_ULTRA_PARALLEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "soc/lcd_cam_struct.h"
#include "hal/gdma_ll.h"

// Revolutionary ESP32-S3 Parallel Processing Engines
#include "esp32s3_etm_engine.h"      // Event Task Matrix - Peripheral-to-Peripheral
#include "esp32s3_gdma_streaming.h"   // GDMA Streaming Pipelines
#include "esp32s3_simd_engine.h"     // SIMD + ESP-DSP Parallel Processing

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// PARALLEL I/O ENGINE CONFIGURATION
// =============================================================================

#define ULTRA_PARALLEL_LCD_CAM_DATA_WIDTH    8      // 8-bit parallel bus
#define ULTRA_PARALLEL_DMA_BUFFER_SIZE       4096   // DMA buffer size
#define ULTRA_PARALLEL_NUM_DMA_BUFFERS       3      // Triple buffering
#define ULTRA_PARALLEL_SIMD_CHUNK_SIZE       256    // SIMD processing chunk

// GPIO pins for parallel I/O demonstration
#define ULTRA_PARALLEL_GPIO_BASE             16     // Base GPIO for 8-bit bus
#define ULTRA_PARALLEL_STROBE_PIN            15     // Strobe/clock pin
#define ULTRA_PARALLEL_ENABLE_PIN            14     // Enable/chip select

// =============================================================================
// MEMORY CAPABILITY DEFINITIONS
// =============================================================================

// High-performance memory allocation strategies
#define ULTRA_PARALLEL_IRAM_CAPS             (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define ULTRA_PARALLEL_DMA_CAPS              (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
#define ULTRA_PARALLEL_PSRAM_CAPS            (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define ULTRA_PARALLEL_SIMD_CAPS             (MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)

// =============================================================================
// ADVANCED DMA STRUCTURES
// =============================================================================

/**
 * @brief GDMA linked-list descriptor for continuous streaming
 */
typedef struct ultra_parallel_dma_desc {
    struct {
        uint32_t size     : 12;   // Buffer size
        uint32_t length   : 12;   // Data length  
        uint32_t reserved : 6;    // Reserved bits
        uint32_t eof      : 1;    // End of frame
        uint32_t owner    : 1;    // DMA ownership
    } ctrl;
    uint8_t* buffer;              // Data buffer pointer
    struct ultra_parallel_dma_desc* next;  // Next descriptor
} ultra_parallel_dma_desc_t;

/**
 * @brief Triple buffer system for zero-copy processing
 */
typedef struct {
    uint8_t* buffers[ULTRA_PARALLEL_NUM_DMA_BUFFERS];  // Buffer array
    ultra_parallel_dma_desc_t descriptors[ULTRA_PARALLEL_NUM_DMA_BUFFERS];
    volatile uint32_t write_idx;    // Current DMA write buffer
    volatile uint32_t read_idx;     // Current CPU read buffer
    volatile uint32_t process_idx;  // Current processing buffer
    RingbufHandle_t processing_queue;  // Inter-core communication
} ultra_parallel_triple_buffer_t;

// =============================================================================
// SIMD PROCESSING STRUCTURES  
// =============================================================================

/**
 * @brief SIMD processing task configuration
 */
typedef struct {
    TaskHandle_t core0_task;      // Core 0 I/O orchestration task
    TaskHandle_t core1_task;      // Core 1 SIMD processing task
    QueueHandle_t data_queue;     // Inter-core data queue
    uint32_t simd_operations;     // SIMD operation counter
    uint64_t processing_time_us;  // Total processing time
} ultra_parallel_simd_config_t;

// =============================================================================
// ULP COPROCESSOR INTEGRATION
// =============================================================================

/**
 * @brief ULP RISC-V background monitoring configuration
 */
typedef struct {
    bool ulp_enabled;             // ULP coprocessor active
    uint32_t gpio_monitoring_mask; // GPIO pins to monitor
    uint32_t adc_sample_rate_hz;  // ADC sampling rate
    uint16_t* ulp_data_buffer;    // ULP→Main core data buffer
    volatile uint32_t ulp_data_count; // Number of ULP samples
} ultra_parallel_ulp_config_t;

// =============================================================================
// MAIN PARALLEL SYSTEM HANDLE
// =============================================================================

/**
 * @brief Ultra-parallel processing system handle - THE COMPLETE PARALLEL BEAST!
 */
typedef struct {
    bool initialized;
    
    // REVOLUTIONARY PARALLEL PROCESSING ENGINES
    etm_engine_handle_t etm_engine;              // Event Task Matrix - Peripheral-to-Peripheral
    gdma_stream_handle_t gdma_stream;            // GDMA Streaming Pipeline - Endless DMA
    simd_engine_handle_t simd_engine;            // SIMD + ESP-DSP - Parallel Lane Processing
    
    // Parallel I/O engines
    ultra_parallel_triple_buffer_t lcd_cam_buffers;
    uint32_t parallel_io_throughput_bps;
    
    // Dual-core SIMD processing  
    ultra_parallel_simd_config_t simd_config;
    
    // ULP coprocessor
    ultra_parallel_ulp_config_t ulp_config;
    
    // Performance metrics
    struct {
        uint64_t dma_transfers;
        uint64_t simd_operations; 
        uint64_t gpio_atomic_writes;
        uint32_t peak_throughput_mbps;
        uint32_t cpu_utilization_percent;
        uint64_t etm_events_processed;           // ETM peripheral-to-peripheral events
        uint32_t gdma_streaming_mbps;            // GDMA streaming throughput
        uint32_t simd_parallel_lanes;            // Active SIMD parallel lanes
    } performance_stats;
    
    // Advanced memory management
    void* iram_hot_code_buffer;   // IRAM for hot ISRs
    void* dma_stream_buffer;      // DMA-capable streaming buffer
    void* psram_bulk_buffer;      // PSRAM for bulk data
    
} ultra_parallel_handle_t;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Initialize ultra-parallel processing system
 * 
 * Sets up all parallel engines:
 * - LCD_CAM peripheral with GDMA
 * - Dual-core task affinity
 * - ULP RISC-V coprocessor
 * - Advanced memory allocation
 * 
 * @param handle Parallel system handle
 * @return ESP_OK on success
 */
esp_err_t ultra_parallel_init(ultra_parallel_handle_t* handle);

/**
 * @brief Start parallel I/O streaming pipeline
 * 
 * Begins continuous data streaming using:
 * - LCD_CAM RX with linked DMA descriptors
 * - Triple buffer zero-copy processing
 * - Core 1 SIMD processing pipeline
 * 
 * @param handle Parallel system handle
 * @param data_source Data source configuration
 * @return ESP_OK on success
 */
esp_err_t ultra_parallel_start_streaming(ultra_parallel_handle_t* handle, void* data_source);

/**
 * @brief Perform atomic GPIO operations
 * 
 * Demonstrates 32-pin simultaneous control for:
 * - Parallel DAC output
 * - LED matrix driving
 * - Strobed bus operations
 * 
 * @param gpio_mask 32-bit GPIO mask
 * @param gpio_values Values to write
 * @return Processing time in microseconds
 */
uint64_t ultra_parallel_gpio_atomic_write(uint32_t gpio_mask, uint32_t gpio_values);

/**
 * @brief Execute SIMD processing on Core 1
 * 
 * Runs hardware-accelerated operations:
 * - FIR filtering with esp-dsp
 * - FFT analysis
 * - Packed arithmetic on 8/16-bit lanes
 * 
 * @param handle Parallel system handle
 * @param input_data Input data array
 * @param output_data Output data array  
 * @param data_length Number of elements
 * @return Processing time in microseconds
 */
uint64_t ultra_parallel_simd_process(ultra_parallel_handle_t* handle, 
                                     const int16_t* input_data,
                                     int16_t* output_data,
                                     size_t data_length);

/**
 * @brief Configure ULP RISC-V background monitoring
 * 
 * Sets up always-on parallel sensing:
 * - GPIO state monitoring
 * - ADC continuous sampling
 * - Main core wake-up triggers
 * 
 * @param handle Parallel system handle
 * @param gpio_mask GPIO pins to monitor
 * @param adc_channels ADC channels to sample
 * @return ESP_OK on success
 */
esp_err_t ultra_parallel_ulp_start_monitoring(ultra_parallel_handle_t* handle,
                                               uint32_t gpio_mask,
                                               uint32_t adc_channels);

/**
 * @brief Get comprehensive performance statistics
 * 
 * Reports parallel processing metrics:
 * - DMA throughput (MB/s)
 * - SIMD operations per second
 * - CPU utilization per core
 * - Memory allocation efficiency
 * 
 * @param handle Parallel system handle
 * @param stats Output statistics structure
 * @return ESP_OK on success  
 */
esp_err_t ultra_parallel_get_performance_stats(ultra_parallel_handle_t* handle, 
                                                void* stats);

/**
 * @brief Demonstrate complete parallel pipeline
 * 
 * Runs comprehensive demonstration:
 * 1. LCD_CAM parallel data capture
 * 2. GDMA streaming to triple buffers
 * 3. Core 1 SIMD processing
 * 4. LCD_CAM parallel data output
 * 5. ULP background monitoring
 * 
 * @param handle Parallel system handle
 * @return ESP_OK on successful demonstration
 */
esp_err_t ultra_parallel_run_full_demo(ultra_parallel_handle_t* handle);

/**
 * @brief Shutdown and cleanup parallel system
 * 
 * @param handle Parallel system handle
 * @return ESP_OK on success
 */
esp_err_t ultra_parallel_deinit(ultra_parallel_handle_t* handle);

// =============================================================================
// PERFORMANCE OPTIMIZATION MACROS
// =============================================================================

// IRAM placement for ultra-fast ISRs
#define ULTRA_PARALLEL_IRAM_ATTR    IRAM_ATTR

// Zero-copy buffer access
#define ULTRA_PARALLEL_ZERO_COPY(buffer) __builtin_assume_aligned(buffer, 4)

// SIMD optimization hints
#define ULTRA_PARALLEL_SIMD_ALIGN   __attribute__((aligned(16)))

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_ULTRA_PARALLEL_H