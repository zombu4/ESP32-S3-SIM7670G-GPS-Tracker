/**
 * ESP32-S3 Hardcore Performance Arsenal Demo Header
 */

#ifndef ESP32S3_HARDCORE_DEMO_H
#define ESP32S3_HARDCORE_DEMO_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Demonstrate hardcore memory allocation strategies
 */
esp_err_t demo_hardcore_memory_allocation(void);

/**
 * Demonstrate IRAM hot loop with prefetching vs standard operations
 */
esp_err_t demo_hardcore_streaming_performance(void);

/**
 * Demonstrate SIMD-style parallel processing capabilities
 */
esp_err_t demo_hardcore_simd_processing(void);

/**
 * Demonstrate power management lock effectiveness
 */
esp_err_t demo_hardcore_power_management(void);

/**
 * Run complete hardcore performance demonstration suite
 */
esp_err_t run_hardcore_performance_demo(void);

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_HARDCORE_DEMO_H