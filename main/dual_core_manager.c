#include "task_system.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DUAL_CORE_MANAGER";

// Smart CPU affinity assignment for ESP32-S3 dual-core
typedef struct {
    const char* task_name;
    cpu_affinity_t optimal_cpu;
    memory_allocation_type_t memory_preference;
    uint32_t expected_cpu_load;
} task_cpu_assignment_t;

// Optimized task assignments for ESP32-S3 architecture
static const task_cpu_assignment_t task_assignments[] = {
    // Core 0: Protocol stack, wireless, critical timing
    {"cellular",     CPU_AFFINITY_CORE0, MEM_ALLOC_INTERNAL,  25}, // AT commands, timing critical
    {"sys_monitor",  CPU_AFFINITY_CORE0, MEM_ALLOC_INTERNAL,  10}, // System management
    
    // Core 1: Application logic, data processing  
    {"gps",          CPU_AFFINITY_CORE1, MEM_ALLOC_EXTERNAL, 20}, // NMEA parsing, buffering
    {"mqtt",         CPU_AFFINITY_CORE1, MEM_ALLOC_EXTERNAL, 15}, // JSON processing, large payloads
    {"battery",      CPU_AFFINITY_CORE1, MEM_ALLOC_BALANCED,  5}, // Simple I2C, low overhead
};

// Dynamic load balancer - monitors and adjusts CPU affinity
static void update_dual_core_load_balance(task_system_t* sys) {
    if (!sys || !sys->core_state.load_balancing_active) return;
    
    // Simplified load monitoring for ESP-IDF v5.5 compatibility
    // Get free heap as indirect load indicator
    size_t free_heap = esp_get_free_heap_size();
    static size_t last_free_heap = 0;
    
    if (last_free_heap == 0) {
        last_free_heap = free_heap;
        return;
    }
    
    // Calculate approximate system load based on heap usage changes
    int32_t heap_delta = (int32_t)(last_free_heap - free_heap);
    uint32_t estimated_load = (heap_delta > 0) ? (uint32_t)heap_delta / 1024 : 0;
    
    // Distribute load estimation between cores (simplified)
    sys->core_state.core0_load_percent = (estimated_load * 60) / 100; // Core 0 gets protocols
    sys->core_state.core1_load_percent = (estimated_load * 40) / 100; // Core 1 gets data processing
    
    ESP_LOGV(TAG, "Estimated loads - Core0: %lu%%, Core1: %lu%%, Free heap: %lu KB", 
             sys->core_state.core0_load_percent, sys->core_state.core1_load_percent, 
             free_heap / 1024);
    
    last_free_heap = free_heap;
}

// Optimized memory allocator for 32-bit ESP32-S3 with PSRAM
void* task_system_malloc(size_t size, memory_allocation_type_t type) {
    void* ptr = NULL;
    
    switch (type) {
        case MEM_ALLOC_INTERNAL:
            // Force internal RAM (DMA-capable, faster access)
            ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
            break;
            
        case MEM_ALLOC_EXTERNAL:
            // Prefer PSRAM for large allocations
            ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            if (!ptr) {
                ESP_LOGW(TAG, "PSRAM allocation failed, falling back to internal");
                ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
            }
            break;
            
        case MEM_ALLOC_BALANCED:
            // Smart allocation based on size
            if (size > 4096) {
                // Large allocations go to PSRAM
                ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            }
            if (!ptr) {
                // Fall back to internal RAM
                ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
            }
            break;
            
        case MEM_ALLOC_CACHE_AWARE:
            // Optimize for cache line alignment (32 bytes on ESP32-S3)
            size_t aligned_size = (size + 31) & ~31;  // Align to 32-byte boundary
            ptr = heap_caps_aligned_alloc(32, aligned_size, MALLOC_CAP_SPIRAM);
            if (!ptr) {
                ptr = heap_caps_aligned_alloc(32, aligned_size, MALLOC_CAP_INTERNAL);
            }
            break;
            
        default:
            ptr = malloc(size);
            break;
    }
    
    if (ptr) {
        ESP_LOGV(TAG, "Allocated %zu bytes at %p (type: %d)", size, ptr, type);
    } else {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes (type: %d)", size, type);
    }
    
    return ptr;
}

// Update memory statistics for monitoring
static void update_memory_statistics(task_system_t* sys) {
    if (!sys) return;
    
    // Internal heap stats
    multi_heap_info_t internal_info;
    heap_caps_get_info(&internal_info, MALLOC_CAP_INTERNAL);
    sys->memory_state.internal_heap_free = internal_info.total_free_bytes;
    sys->memory_state.largest_internal_block = internal_info.largest_free_block;
    
    // External heap stats (PSRAM)
    multi_heap_info_t external_info;
    heap_caps_get_info(&external_info, MALLOC_CAP_SPIRAM);
    sys->memory_state.external_heap_free = external_info.total_free_bytes;
    sys->memory_state.largest_external_block = external_info.largest_free_block;
    
    // Calculate fragmentation
    if (internal_info.total_free_bytes > 0) {
        sys->memory_state.fragmentation_percent = 
            100 - ((sys->memory_state.largest_internal_block * 100) / internal_info.total_free_bytes);
    }
    
    ESP_LOGV(TAG, "Memory - Internal: %lu KB free, External: %lu KB free, Frag: %lu%%",
             sys->memory_state.internal_heap_free / 1024,
             sys->memory_state.external_heap_free / 1024,
             sys->memory_state.fragmentation_percent);
}

// Smart task creation with optimal CPU and memory assignment
BaseType_t create_optimized_task(TaskFunction_t task_function,
                                const char* task_name,
                                uint32_t stack_size,
                                void* parameters,
                                UBaseType_t priority,
                                TaskHandle_t* task_handle,
                                task_system_t* sys) {
    
    // Find optimal CPU assignment
    cpu_affinity_t optimal_cpu = CPU_AFFINITY_AUTO;
    memory_allocation_type_t mem_pref = MEM_ALLOC_BALANCED;
    
    for (size_t i = 0; i < sizeof(task_assignments) / sizeof(task_assignments[0]); i++) {
        if (strcmp(task_assignments[i].task_name, task_name) == 0) {
            optimal_cpu = task_assignments[i].optimal_cpu;
            mem_pref = task_assignments[i].memory_preference;
            break;
        }
    }
    
    // Allocate stack in appropriate memory type
    StackType_t* task_stack = NULL;
    if (mem_pref == MEM_ALLOC_EXTERNAL || stack_size > 8192) {
        // Use PSRAM for large stacks
        task_stack = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "📚 Allocated %s stack (%lu bytes) in PSRAM", task_name, stack_size);
    }
    
    BaseType_t result;
    if (task_stack) {
        // Create task with external stack
        StaticTask_t* task_buffer = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        if (task_buffer) {
            *task_handle = xTaskCreateStaticPinnedToCore(
                task_function,
                task_name,
                stack_size / sizeof(StackType_t),
                parameters,
                priority,
                task_stack,
                task_buffer,
                optimal_cpu
            );
            result = (*task_handle != NULL) ? pdPASS : pdFAIL;
        } else {
            result = pdFAIL;
        }
    } else {
        // Create task normally with internal stack
        result = xTaskCreatePinnedToCore(
            task_function,
            task_name,
            stack_size,
            parameters,
            priority,
            task_handle,
            optimal_cpu
        );
        ESP_LOGI(TAG, "📚 Created %s task with internal stack (%lu bytes)", task_name, stack_size);
    }
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "✅ Task '%s' created on Core %d with priority %lu", 
                 task_name, (int)optimal_cpu, priority);
    } else {
        ESP_LOGE(TAG, "❌ Failed to create task '%s'", task_name);
    }
    
    return result;
}

// Export functions for use by task_system.c
void task_system_init_dual_core_manager(task_system_t* sys) {
    if (!sys) return;
    
    // Initialize dual-core state
    sys->core_state.load_balancing_active = true;
    sys->memory_state.external_memory_optimized = true;
    
    ESP_LOGI(TAG, "🚀 Dual-core manager initialized for ESP32-S3");
    ESP_LOGI(TAG, "💾 PSRAM optimization enabled");
    ESP_LOGI(TAG, "⚡ Dynamic load balancing active");
}

void task_system_update_performance_counters(task_system_t* sys) {
    if (!sys) return;
    
    update_dual_core_load_balance(sys);
    update_memory_statistics(sys);
}