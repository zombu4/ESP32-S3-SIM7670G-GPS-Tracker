/**
 * ESP32-S3 Cellular Connection Ultra-Verbose Debug System
 * 
 * COMPREHENSIVE TROUBLESHOOTING for cellular timeout issues:
 * - Real-time AT command monitoring with timing
 * - SIM card status verification 
 * - Network registration detailed analysis
 * - Signal strength and operator detection
 * - Hardware interface validation
 * - Step-by-step connection diagnostics
 */

#ifndef CELLULAR_DEBUG_ULTRA_H
#define CELLULAR_DEBUG_ULTRA_H

#include "esp_log.h"
#include "esp_timer.h"
#include "modules/lte/lte_module.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ultra-verbose debug levels
#define CELLULAR_DEBUG_LEVEL_SILENT    0
#define CELLULAR_DEBUG_LEVEL_BASIC     1
#define CELLULAR_DEBUG_LEVEL_DETAILED  2
#define CELLULAR_DEBUG_LEVEL_ULTRA     3

// Current debug level (set to ULTRA for troubleshooting)
#define CURRENT_DEBUG_LEVEL            CELLULAR_DEBUG_LEVEL_ULTRA

// Ultra-verbose logging macros
#define CELLULAR_ULTRA_LOG(format, ...) \
    if (CURRENT_DEBUG_LEVEL >= CELLULAR_DEBUG_LEVEL_ULTRA) { \
        ESP_LOGI("CELLULAR_ULTRA", "⚡[%llu] " format, esp_timer_get_time() / 1000, ##__VA_ARGS__); \
    }

#define CELLULAR_DETAILED_LOG(format, ...) \
    if (CURRENT_DEBUG_LEVEL >= CELLULAR_DEBUG_LEVEL_DETAILED) { \
        ESP_LOGI("CELLULAR_DEBUG", "🔍 " format, ##__VA_ARGS__); \
    }

#define CELLULAR_AT_CMD_LOG(cmd, response, duration_ms) \
    if (CURRENT_DEBUG_LEVEL >= CELLULAR_DEBUG_LEVEL_ULTRA) { \
        ESP_LOGI("AT_CMD_MONITOR", "📡 CMD: '%s' | RSP: '%s' | TIME: %lu ms", \
                 cmd ? cmd : "NULL", response ? response : "NULL", duration_ms); \
    }

#define CELLULAR_STEP_LOG(step, description) \
    if (CURRENT_DEBUG_LEVEL >= CELLULAR_DEBUG_LEVEL_DETAILED) { \
        ESP_LOGI("CELLULAR_STEPS", "🎯 STEP %d: %s", step, description); \
    }

#define CELLULAR_ERROR_LOG(error_description) \
    ESP_LOGE("CELLULAR_ERROR", "❌ ERROR: %s", error_description)

// Diagnostic test results
typedef struct {
    bool hardware_ok;
    bool sim_card_detected;
    bool sim_pin_ok;
    bool network_available;
    bool registration_ok;
    bool signal_strength_ok;
    bool apn_configured;
    bool data_connection_ok;
    int signal_rssi;
    char operator_name[64];
    char sim_iccid[32];
    char error_details[256];
} cellular_diagnostic_t;

/**
 * Initialize ultra-verbose cellular debugging system
 */
esp_err_t cellular_debug_init(void);

/**
 * Run comprehensive cellular hardware diagnostic
 */
esp_err_t cellular_run_hardware_diagnostic(cellular_diagnostic_t* results);

/**
 * Run SIM card diagnostic tests
 */
esp_err_t cellular_run_sim_diagnostic(const lte_interface_t* lte_if, cellular_diagnostic_t* results);

/**
 * Run network connectivity diagnostic
 */
esp_err_t cellular_run_network_diagnostic(const lte_interface_t* lte_if, cellular_diagnostic_t* results);

/**
 * Monitor AT command execution with timing
 */
bool cellular_debug_at_command(const lte_interface_t* lte_if, const char* command, 
                               char* response_buffer, size_t buffer_size, int timeout_ms);

/**
 * Log detailed system state for troubleshooting
 */
void cellular_debug_log_system_state(void);

/**
 * Comprehensive connection troubleshooting sequence
 */
esp_err_t cellular_troubleshoot_connection(const lte_interface_t* lte_if);

/**
 * Generate diagnostic report with recommendations
 */
void cellular_generate_diagnostic_report(const cellular_diagnostic_t* results);

/**
 * Test specific cellular functionality with ultra-verbose output
 */
esp_err_t cellular_test_functionality(const lte_interface_t* lte_if);

#ifdef __cplusplus
}
#endif

#endif // CELLULAR_DEBUG_ULTRA_H