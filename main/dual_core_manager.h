#pragma once

#include "task_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// Smart memory allocator for 32-bit ESP32-S3 with PSRAM
void* task_system_malloc(size_t size, memory_allocation_type_t type);

// Optimized task creation with CPU affinity and memory management
BaseType_t create_optimized_task(TaskFunction_t task_function,
                                const char* task_name,
                                uint32_t stack_size,
                                void* parameters,
                                UBaseType_t priority,
                                TaskHandle_t* task_handle,
                                task_system_t* sys);

// Initialize dual-core management system
void task_system_init_dual_core_manager(task_system_t* sys);

// Update performance counters and load balancing
void task_system_update_performance_counters(task_system_t* sys);

#ifdef __cplusplus
}
#endif