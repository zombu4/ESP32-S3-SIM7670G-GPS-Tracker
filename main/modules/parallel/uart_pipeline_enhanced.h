/**
 * UART PIPELINE ENHANCED - GPS/Cellular Conflict Resolution
 * 
 * Simplified version using standard UART with buffering to resolve 
 * GPS/cellular interference without requiring private GDMA APIs.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// 🔥 ENHANCED UART PIPELINE CONFIGURATION 🔥

#define ENHANCED_UART_PORT           UART_NUM_1
#define ENHANCED_BUFFER_SIZE         8192     // Large UART buffer
#define ENHANCED_RING_BUFFER_SIZE    32768    // Ring buffer for processing
#define ENHANCED_GPS_QUEUE_SIZE      16       // GPS message queue
#define ENHANCED_CELLULAR_QUEUE_SIZE 32       // Cellular response queue
#define ENHANCED_TASK_STACK_SIZE     8192     // Task stack size
#define ENHANCED_TASK_PRIORITY       10       // High priority for real-time

// Stream Detection Types
typedef enum {
    STREAM_TYPE_UNKNOWN = 0,
    STREAM_TYPE_GPS,        // NMEA GPS data ($GNRMC, $GNGGA, etc.)
    STREAM_TYPE_CELLULAR,   // AT command responses (+CREG, OK, ERROR, etc.)
    STREAM_TYPE_DEBUG       // Debug or other data
} enhanced_stream_type_t;

// Enhanced Message Structure
typedef struct {
    enhanced_stream_type_t type;
    uint32_t timestamp_ms;
    uint16_t length;
    char data[512];         // Message data
} enhanced_message_t;

// Enhanced Pipeline Statistics
typedef struct {
    uint32_t total_messages;
    uint32_t gps_messages;
    uint32_t cellular_messages;
    uint32_t parse_errors;
    uint32_t buffer_overflows;
    uint32_t last_gps_time;
    uint32_t last_cellular_time;
} enhanced_pipeline_stats_t;

// Enhanced Pipeline Structure
typedef struct {
    // UART Configuration
    bool initialized;
    uart_config_t uart_config;
    
    // Buffering System
    RingbufHandle_t ring_buffer;
    uint8_t *uart_buffer;
    
    // Message Queues
    QueueHandle_t gps_queue;
    QueueHandle_t cellular_queue;
    
    // Task Handles
    TaskHandle_t reader_task;
    TaskHandle_t parser_task;
    
    // Synchronization
    SemaphoreHandle_t stats_mutex;
    
    // Statistics
    enhanced_pipeline_stats_t stats;
    
    // Configuration
    bool debug_enabled;
    
} enhanced_uart_pipeline_t;

// 🔥 ENHANCED PIPELINE API 🔥

/**
 * Initialize enhanced UART pipeline system
 */
esp_err_t enhanced_uart_pipeline_init(enhanced_uart_pipeline_t *pipeline);

/**
 * Start the enhanced pipeline (creates tasks)
 */
esp_err_t enhanced_uart_pipeline_start(enhanced_uart_pipeline_t *pipeline);

/**
 * Stop the enhanced pipeline
 */
esp_err_t enhanced_uart_pipeline_stop(enhanced_uart_pipeline_t *pipeline);

/**
 * Get GPS message (non-blocking)
 */
esp_err_t enhanced_pipeline_get_gps_message(enhanced_uart_pipeline_t *pipeline, 
                                           enhanced_message_t *message, 
                                           uint32_t timeout_ms);

/**
 * Get cellular message (non-blocking)
 */
esp_err_t enhanced_pipeline_get_cellular_message(enhanced_uart_pipeline_t *pipeline, 
                                                enhanced_message_t *message, 
                                                uint32_t timeout_ms);

/**
 * Send command to UART (for AT commands)
 */
esp_err_t enhanced_pipeline_send_command(enhanced_uart_pipeline_t *pipeline, 
                                        const char *command, size_t length);

/**
 * Get pipeline statistics
 */
esp_err_t enhanced_pipeline_get_stats(enhanced_uart_pipeline_t *pipeline, 
                                     enhanced_pipeline_stats_t *stats);

/**
 * Reset pipeline statistics
 */
esp_err_t enhanced_pipeline_reset_stats(enhanced_uart_pipeline_t *pipeline);

/**
 * Enable/disable debug output
 */
esp_err_t enhanced_pipeline_set_debug(enhanced_uart_pipeline_t *pipeline, bool enabled);

// 🔥 STREAM DETECTION FUNCTIONS 🔥

/**
 * Detect stream type from message content
 */
enhanced_stream_type_t enhanced_detect_stream_type(const char *data, size_t length);

/**
 * Check if message is complete GPS sentence
 */
bool enhanced_is_complete_gps_message(const char *data, size_t length);

/**
 * Check if message is complete AT response
 */
bool enhanced_is_complete_at_response(const char *data, size_t length);

#endif // UART_PIPELINE_ENHANCED_H