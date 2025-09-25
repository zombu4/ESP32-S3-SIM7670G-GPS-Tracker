/**
 * @file task_manager.h
 * @brief Dual-core task management for ESP32-S3
 * 
 * ESP32-S3 Dual-Core Architecture:
 * - Core 0: Protocol tasks (LTE, MQTT, AT commands)
 * - Core 1: Application tasks (GPS, Battery, Data collection)
 * 
 * All operations are non-blocking with FreeRTOS queues for inter-core communication
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "modules/gps/gps_module.h"
#include "modules/battery/battery_module.h"
#include "modules/mqtt/mqtt_module.h"
#include "modules/lte/lte_module.h"

// Core assignments
#define PROTOCOL_CORE   0   // Core 0: LTE, MQTT, AT commands
#define APPLICATION_CORE 1  // Core 1: GPS, Battery, Sensors

// Task priorities (0 = lowest, 25 = highest)
#define PRIORITY_HIGH       20
#define PRIORITY_NORMAL     10
#define PRIORITY_LOW        5

// Stack sizes (in words, not bytes)
#define STACK_SIZE_LARGE    8192
#define STACK_SIZE_MEDIUM   4096
#define STACK_SIZE_SMALL    2048

// Queue sizes
#define GPS_DATA_QUEUE_SIZE     10
#define BATTERY_DATA_QUEUE_SIZE 5
#define MQTT_PUBLISH_QUEUE_SIZE 20
#define AT_COMMAND_QUEUE_SIZE   10

// Note: Use module-defined structures directly
// #include "modules/gps/gps_module.h" for gps_data_t
// #include "modules/battery/battery_module.h" for battery_data_t

/**
 * @brief MQTT publish message structure
 */
typedef struct {
    char topic[64];
    char payload[512];
    int qos;
    bool retain;
} mqtt_publish_msg_t;

/**
 * @brief AT command structure for queued processing
 */
typedef struct {
    char command[128];
    char response[512];
    uint32_t timeout_ms;
    bool success;
    SemaphoreHandle_t completion_sem;
} at_command_msg_t;

/**
 * @brief Task manager interface
 */
typedef struct {
    // Task handles
    TaskHandle_t lte_task;
    TaskHandle_t mqtt_task;
    TaskHandle_t gps_task;
    TaskHandle_t battery_task;
    TaskHandle_t watchdog_task;
    
    // Inter-core communication queues
    QueueHandle_t gps_data_queue;
    QueueHandle_t battery_data_queue;
    QueueHandle_t mqtt_publish_queue;
    QueueHandle_t at_command_queue;
    
    // Synchronization
    SemaphoreHandle_t shared_uart_mutex;
    
    // Status flags
    bool tasks_running;
    bool system_ready;
    
    // Methods
    bool (*init)(void);
    bool (*start_all_tasks)(void);
    void (*stop_all_tasks)(void);
    bool (*send_gps_data)(gps_data_t* data);
    bool (*send_battery_data)(battery_data_t* data);
    bool (*publish_mqtt)(const char* topic, const char* payload, int qos);
    bool (*send_at_command)(const char* command, char* response, uint32_t timeout_ms);
    void (*feed_watchdog)(void);
} task_manager_t;

/**
 * @brief Get task manager interface
 */
const task_manager_t* task_manager_get_interface(void);

// Core 0 Tasks (Protocol Core)
void lte_management_task(void* params);
void mqtt_communication_task(void* params);
void system_watchdog_task(void* params);

// Core 1 Tasks (Application Core)  
void gps_data_collection_task(void* params);
void battery_monitoring_task(void* params);

#endif // TASK_MANAGER_H