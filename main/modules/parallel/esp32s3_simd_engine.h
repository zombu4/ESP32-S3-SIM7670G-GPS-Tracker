/**
 * ESP32-S3 SIMD + ESP-DSP Engine
 * 
 * PARALLEL PROCESSING POWERHOUSE: Unleashes ESP32-S3's Xtensa LX7 packed
 * SIMD instructions combined with ESP-DSP library for maximum computational
 * throughput while DMA engines handle I/O in parallel!
 * 
 * Revolutionary Features:
 * - 4×8-bit and 2×16-bit lane parallel processing
 * - Hardware-accelerated FIR, FFT, convolution, dot-products
 * - Optimized memory layout for SIMD operations
 * - Integration with GDMA streaming pipelines
 * - Real-time signal processing capabilities
 */

#ifndef ESP32S3_SIMD_ENGINE_H
#define ESP32S3_SIMD_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

// SIMD Engine Configuration
#define SIMD_VECTOR_SIZE_8BIT        4    // 4×8-bit lanes per operation
#define SIMD_VECTOR_SIZE_16BIT       2    // 2×16-bit lanes per operation
#define SIMD_ALIGNMENT_BYTES         4    // 32-bit alignment requirement
#define SIMD_MAX_BUFFER_SIZE         (64*1024)  // Maximum buffer size for SIMD ops

// SIMD Operation Types
typedef enum {
    SIMD_OP_ADD_SATURATE = 0,      // Saturating addition
    SIMD_OP_SUB_SATURATE,          // Saturating subtraction
    SIMD_OP_MUL_HIGH,              // Multiplication with high part
    SIMD_OP_MAC,                   // Multiply-accumulate
    SIMD_OP_COMPARE,               // Parallel compare
    SIMD_OP_MIN_MAX,               // Min/Max operations
    SIMD_OP_SHIFT_PACK,            // Shift and pack
    SIMD_OP_CUSTOM                 // Custom SIMD operation
} simd_operation_t;

// SIMD Data Types
typedef union {
    uint32_t u32;                  // 32-bit unsigned integer
    int32_t  i32;                  // 32-bit signed integer
    struct {
        uint8_t b0, b1, b2, b3;    // 4×8-bit bytes
    } u8x4;
    struct {
        uint16_t h0, h1;           // 2×16-bit halfwords
    } u16x2;
} simd_vector_t;

// SIMD Engine Handle
typedef struct simd_engine_s* simd_engine_handle_t;

// SIMD Configuration
typedef struct {
    bool enable_esp_dsp;           // Enable ESP-DSP library functions
    size_t working_buffer_size;    // Working buffer size for operations
    bool enable_performance_counters; // Enable performance measurement
    uint8_t optimization_level;    // Optimization level (0-3)
} simd_engine_config_t;

// SIMD Performance Statistics
typedef struct {
    uint64_t operations_performed;     // Total SIMD operations performed
    uint64_t bytes_processed;          // Total bytes processed
    uint32_t peak_mops;                // Peak million operations per second
    uint32_t current_throughput_mbps;  // Current throughput MB/s
    float simd_efficiency_percent;     // SIMD utilization efficiency
    uint32_t cache_hits;               // Cache hit count
    uint32_t cache_misses;             // Cache miss count
} simd_performance_stats_t;

// Custom SIMD Function Type
typedef void (*simd_custom_func_t)(const void* input, void* output, size_t count, void* params);

/**
 * Initialize SIMD Engine
 * 
 * Unlocks the ESP32-S3's parallel processing capabilities with optimized
 * memory allocation and ESP-DSP integration.
 * 
 * @param config SIMD engine configuration
 * @param handle Output handle for SIMD engine
 * @return ESP_OK on success
 */
esp_err_t simd_engine_init(const simd_engine_config_t* config, simd_engine_handle_t* handle);

/**
 * 4×8-bit Saturating Add - PARALLEL LANES!
 * 
 * REVOLUTIONARY: Adds 4 pairs of 8-bit values simultaneously in a single
 * instruction with automatic saturation to prevent overflow!
 * 
 * Example: [255,100,50,200] + [10,50,75,100] = [255,150,125,255] (saturated)
 * 
 * @param handle SIMD engine handle
 * @param a First vector (4×8-bit values)
 * @param b Second vector (4×8-bit values)
 * @return Result vector with saturated addition
 */
simd_vector_t simd_add4_u8_saturate(simd_engine_handle_t handle, simd_vector_t a, simd_vector_t b);

/**
 * 2×16-bit Multiply-Accumulate - DUAL LANE MAC!
 * 
 * POWERHOUSE OPERATION: Performs two 16-bit multiply-accumulate operations
 * in parallel - perfect for FIR filters and signal processing!
 * 
 * @param handle SIMD engine handle
 * @param a First vector (2×16-bit values)
 * @param b Second vector (2×16-bit values)
 * @param accumulator Accumulator vector
 * @return Result vector with MAC results
 */
simd_vector_t simd_mac2_u16(simd_engine_handle_t handle, simd_vector_t a, simd_vector_t b, simd_vector_t accumulator);

/**
 * Parallel Min/Max Operations
 * 
 * INSTANT STATISTICS: Find minimum and maximum values across multiple
 * lanes simultaneously - perfect for image processing and analytics!
 * 
 * @param handle SIMD engine handle
 * @param data Input data array
 * @param length Data length (must be multiple of 4)
 * @param min_result Output minimum values
 * @param max_result Output maximum values
 * @return ESP_OK on success
 */
esp_err_t simd_parallel_minmax_u8(simd_engine_handle_t handle, 
                                  const uint8_t* data, 
                                  size_t length,
                                  simd_vector_t* min_result, 
                                  simd_vector_t* max_result);

/**
 * SIMD Memory Copy with Processing
 * 
 * ZERO-COPY PROCESSING: Copy data while applying SIMD operations
 * simultaneously - ultimate efficiency!
 * 
 * @param handle SIMD engine handle
 * @param src Source buffer
 * @param dst Destination buffer  
 * @param length Buffer length
 * @param operation SIMD operation to apply during copy
 * @param params Operation parameters
 * @return ESP_OK on success
 */
esp_err_t simd_memcpy_with_processing(simd_engine_handle_t handle,
                                      const void* src,
                                      void* dst,
                                      size_t length,
                                      simd_operation_t operation,
                                      void* params);

/**
 * ESP-DSP Hardware Accelerated FIR Filter
 * 
 * SIGNAL PROCESSING BEAST: Hardware-accelerated FIR filtering using
 * ESP32-S3's optimized DSP instructions!
 * 
 * @param handle SIMD engine handle
 * @param input Input signal buffer
 * @param output Output filtered buffer
 * @param length Signal length
 * @param coeffs FIR filter coefficients
 * @param coeff_count Number of coefficients
 * @return ESP_OK on success
 */
esp_err_t simd_fir_filter_f32(simd_engine_handle_t handle,
                               const float* input,
                               float* output,
                               size_t length,
                               const float* coeffs,
                               size_t coeff_count);

/**
 * ESP-DSP Hardware Accelerated FFT
 * 
 * FREQUENCY DOMAIN POWERHOUSE: Hardware-accelerated Fast Fourier Transform
 * with optimized complex number processing!
 * 
 * @param handle SIMD engine handle
 * @param input Complex input buffer (real, imag, real, imag...)
 * @param output Complex output buffer
 * @param length FFT length (must be power of 2)
 * @param inverse true for inverse FFT, false for forward FFT
 * @return ESP_OK on success
 */
esp_err_t simd_fft_complex_f32(simd_engine_handle_t handle,
                                const float* input,
                                float* output,
                                size_t length,
                                bool inverse);

/**
 * Parallel Convolution Engine
 * 
 * IMAGE PROCESSING BEAST: Parallel convolution for image filtering,
 * edge detection, and signal processing applications!
 * 
 * @param handle SIMD engine handle
 * @param input Input data array
 * @param kernel Convolution kernel
 * @param output Output result array
 * @param width Data width
 * @param height Data height
 * @param kernel_size Kernel size (NxN)
 * @return ESP_OK on success
 */
esp_err_t simd_convolution_2d_u8(simd_engine_handle_t handle,
                                  const uint8_t* input,
                                  const int16_t* kernel,
                                  uint8_t* output,
                                  size_t width,
                                  size_t height,
                                  size_t kernel_size);

/**
 * SIMD Vector Dot Product
 * 
 * MATHEMATICAL POWERHOUSE: Hardware-accelerated dot product computation
 * using parallel multiplication and accumulation!
 * 
 * @param handle SIMD engine handle
 * @param vector_a First vector
 * @param vector_b Second vector
 * @param length Vector length (must be multiple of 4)
 * @param result Output dot product result
 * @return ESP_OK on success
 */
esp_err_t simd_dot_product_f32(simd_engine_handle_t handle,
                                const float* vector_a,
                                const float* vector_b,
                                size_t length,
                                float* result);

/**
 * Register Custom SIMD Function
 * 
 * EXTENSIBLE ARCHITECTURE: Register custom SIMD operations for
 * application-specific parallel processing needs!
 * 
 * @param handle SIMD engine handle
 * @param func Custom SIMD function
 * @param operation_id User-defined operation ID
 * @return ESP_OK on success
 */
esp_err_t simd_register_custom_function(simd_engine_handle_t handle,
                                        simd_custom_func_t func,
                                        uint32_t operation_id);

/**
 * Execute Custom SIMD Operation
 * 
 * @param handle SIMD engine handle
 * @param operation_id Custom operation ID
 * @param input Input data
 * @param output Output data
 * @param length Data length
 * @param params Operation parameters
 * @return ESP_OK on success
 */
esp_err_t simd_execute_custom_operation(simd_engine_handle_t handle,
                                        uint32_t operation_id,
                                        const void* input,
                                        void* output,
                                        size_t length,
                                        void* params);

/**
 * Enable SIMD Fast Path Mode
 * 
 * MAXIMUM COMPUTATIONAL UNLOCK: Optimizes CPU for sustained SIMD operations
 * with cache optimization and frequency locking.
 * 
 * @param handle SIMD engine handle
 * @return ESP_OK on success
 */
esp_err_t simd_enable_fast_path_mode(simd_engine_handle_t handle);

/**
 * Get SIMD Performance Statistics
 * 
 * @param handle SIMD engine handle
 * @param stats Output performance statistics
 * @return ESP_OK on success
 */
esp_err_t simd_get_performance_stats(simd_engine_handle_t handle, simd_performance_stats_t* stats);

/**
 * SIMD Engine Demonstration
 * 
 * Showcases the parallel processing capabilities:
 * 1. 4×8-bit lane parallel operations
 * 2. Hardware-accelerated DSP functions
 * 3. Real-time signal processing
 * 4. Performance measurement and optimization
 * 
 * @param handle SIMD engine handle
 * @return ESP_OK on success
 */
esp_err_t simd_run_demonstration(simd_engine_handle_t handle);

/**
 * Cleanup SIMD Engine
 * 
 * @param handle SIMD engine handle
 * @return ESP_OK on success
 */
esp_err_t simd_engine_deinit(simd_engine_handle_t handle);

// Default configuration macro
#define SIMD_ENGINE_DEFAULT_CONFIG() {              \
    .enable_esp_dsp = true,                         \
    .working_buffer_size = 8192,                    \
    .enable_performance_counters = true,            \
    .optimization_level = 3                         \
}

// High-performance configuration
#define SIMD_ENGINE_HIGH_PERFORMANCE_CONFIG() {     \
    .enable_esp_dsp = true,                         \
    .working_buffer_size = 32768,                   \
    .enable_performance_counters = false,           \
    .optimization_level = 3                         \
}

// Utility macros for SIMD vector creation
#define SIMD_PACK_U8X4(b0, b1, b2, b3) \
    ((simd_vector_t){.u8x4 = {b0, b1, b2, b3}})

#define SIMD_PACK_U16X2(h0, h1) \
    ((simd_vector_t){.u16x2 = {h0, h1}})

#define SIMD_PACK_U32(value) \
    ((simd_vector_t){.u32 = value})

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_SIMD_ENGINE_H