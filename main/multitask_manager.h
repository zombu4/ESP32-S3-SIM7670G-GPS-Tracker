#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced multitasking manager for ESP32-S3 dual-core
typedef struct {
    TaskHandle_t handle;
    const char* name;
    uint32_t stack_size;
    UBaseType_t priority;
    BaseType_t core_affinity;
    size_t high_water_mark;
    uint32_t cpu_time_us;
    bool is_background;
    void* task_data;
} concurrent_task_t;

typedef struct {
    concurrent_task_t tasks[8];
    uint8_t task_count;
    SemaphoreHandle_t manager_mutex;
    bool scheduler_active;
    uint32_t total_cpu_time;
    QueueHandle_t background_queue;
    TaskHandle_t scheduler_task;
} multitask_manager_t;

// Background task processing
typedef struct {
    void (*function)(void* params);
    void* parameters;
    const char* description;
    uint32_t priority_level;
} background_job_t;

// Initialize enhanced multitasking system
bool multitask_init(void);

// Create concurrent task with automatic load balancing
BaseType_t multitask_create_concurrent(
    TaskFunction_t task_function,
    const char* task_name,
    uint32_t stack_size,
    void* parameters,
    UBaseType_t priority,
    TaskHandle_t* task_handle,
    bool is_background
);

// Submit background job for async processing
bool multitask_submit_background_job(
    void (*function)(void* params),
    void* parameters,
    const char* description,
    uint32_t priority
);

// Dynamic stack size adjustment
bool multitask_adjust_stack_size(TaskHandle_t task, uint32_t new_stack_size);

// Get task performance statistics
bool multitask_get_task_stats(const char* task_name, concurrent_task_t* stats);

// Enable/disable dynamic scheduling
void multitask_set_dynamic_scheduling(bool enabled);

// Monitor system load and auto-balance
void multitask_monitor_and_balance(void);

#ifdef __cplusplus
}
#endif