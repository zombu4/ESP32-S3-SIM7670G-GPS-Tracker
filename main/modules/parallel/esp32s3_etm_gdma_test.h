/**
 * ESP32-S3 GDMA + SIMD Revolutionary Test Header
 * Note: ETM not supported on ESP32-S3 (available on ESP32-C6, ESP32-P4, ESP32-H2)
 */

#ifndef ESP32S3_ETM_GDMA_TEST_H
#define ESP32S3_ETM_GDMA_TEST_H

#include "esp_err.h"
#include "esp_private/gdma.h" // GDMA driver - CONFIRMED AVAILABLE!

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Test ESP32-S3 revolutionary parallel processing capabilities overview
 */
esp_err_t test_esp32s3_capabilities(void);

/**
 * Test ESP-IDF GDMA Channel Allocation and Management
 */
esp_err_t test_gdma_channels(void);

/**
 * Test ESP32-S3 SIMD instruction capabilities
 */
esp_err_t test_simd_instructions(void);

/**
 * Run complete ESP32-S3 revolutionary parallel processing test suite
 */
esp_err_t run_etm_gdma_revolutionary_test(void);

#ifdef __cplusplus
}
#endif

#endif // ESP32S3_ETM_GDMA_TEST_H