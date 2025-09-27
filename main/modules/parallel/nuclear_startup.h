/**
 * 💀🔥💀 NUCLEAR PIPELINE STARTUP MANAGER 💀🔥💀
 * 
 * Integrates nuclear pipeline with main GPS tracker startup
 */

#pragma once

#include "modules/parallel/nuclear_integration.h"
#include <stdbool.h>

// 🚀 NUCLEAR STARTUP CONFIGURATION
typedef struct {
    bool enable_nuclear_pipeline;
    bool enable_debug_logging;
    bool enable_performance_monitoring;
} nuclear_startup_config_t;

// 💀🔥 NUCLEAR STARTUP API 🔥💀

/**
 * Initialize nuclear pipeline system during GPS tracker startup
 */
esp_err_t nuclear_startup_init(const nuclear_startup_config_t *config);

/**
 * Start nuclear pipeline (called after basic system initialization)
 */
esp_err_t nuclear_startup_begin(void);

/**
 * Check if nuclear pipeline is ready
 */
bool nuclear_startup_is_ready(void);

/**
 * Get nuclear pipeline statistics for monitoring
 */
void nuclear_startup_get_performance_stats(uint32_t *total_bytes, 
                                         uint32_t *cellular_packets,
                                         uint32_t *gps_packets,
                                         uint32_t *errors);

/**
 * Shutdown nuclear pipeline system
 */
esp_err_t nuclear_startup_shutdown(void);

// Default configuration
extern const nuclear_startup_config_t NUCLEAR_STARTUP_DEFAULT_CONFIG;