/**
 * 💀🔥💀 NUCLEAR PIPELINE INTEGRATION MANAGER 💀🔥💀
 * 
 * Connects the nuclear GDMA+ETM pipeline to GPS and Cellular modules
 * Provides seamless integration with existing codebase
 */

#pragma once

#include "uart_pipeline_nuclear.h"
#include "../gps/gps_module.h"
#include "../lte/lte_module.h"
#include <stdbool.h>

// 🚀 INTEGRATION CONFIGURATION
#define NUCLEAR_INTEGRATION_TASK_STACK_SIZE    8192
#define NUCLEAR_INTEGRATION_TASK_PRIORITY      25
#define NUCLEAR_GPS_READ_TIMEOUT_MS           1000
#define NUCLEAR_CELLULAR_READ_TIMEOUT_MS      2000

// Integration state
typedef enum {
    NUCLEAR_STATE_UNINITIALIZED = 0,
    NUCLEAR_STATE_INITIALIZING,
    NUCLEAR_STATE_RUNNING,
    NUCLEAR_STATE_ERROR,
    NUCLEAR_STATE_SHUTDOWN
} nuclear_integration_state_t;

// Integration manager structure
typedef struct {
    nuclear_uart_pipeline_t *pipeline;
    nuclear_integration_state_t state;
    
    // Task handles
    TaskHandle_t gps_reader_task;
    TaskHandle_t cellular_reader_task;
    
    // Statistics
    uint32_t gps_reads_completed;
    uint32_t cellular_reads_completed;
    uint32_t integration_errors;
    
    // Control flags
    volatile bool integration_active;
} nuclear_integration_manager_t;

// 💀🔥 NUCLEAR INTEGRATION API 🔥💀

/**
 * Initialize the nuclear integration system
 */
esp_err_t nuclear_integration_init(nuclear_integration_manager_t *manager);

/**
 * Start nuclear integration (replaces existing UART handling)
 */
esp_err_t nuclear_integration_start(nuclear_integration_manager_t *manager);

/**
 * Stop nuclear integration
 */
esp_err_t nuclear_integration_stop(nuclear_integration_manager_t *manager);

/**
 * Cleanup nuclear integration
 */
esp_err_t nuclear_integration_deinit(nuclear_integration_manager_t *manager);

/**
 * Get integration statistics
 */
void nuclear_integration_get_stats(nuclear_integration_manager_t *manager,
                                  uint32_t *gps_reads,
                                  uint32_t *cellular_reads, 
                                  uint32_t *errors);

/**
 * Nuclear GPS data reader (replaces existing GPS polling)
 */
bool nuclear_gps_read_data(gps_data_t* data);

/**
 * Nuclear AT command sender (replaces existing AT interface)
 */
bool nuclear_send_at_command(const char* command, char* response, size_t response_size, int timeout_ms);

/**
 * Check if nuclear integration is active
 */
bool nuclear_integration_is_active(void);

// Internal task functions (forward declarations - implemented in nuclear_integration.c)
void nuclear_gps_reader_task(void *parameters);
void nuclear_cellular_reader_task(void *parameters);

// Global integration manager
extern nuclear_integration_manager_t *g_nuclear_integration;