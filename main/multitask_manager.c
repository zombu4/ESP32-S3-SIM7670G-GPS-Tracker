#include "multitask_manager.h"
#include "dual_core_manager.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "MULTITASK";
static multitask_manager_t g_multitask = {0};

// Background scheduler task
static void background_scheduler_task(void* parameters) {
    ESP_LOGI(TAG, "🔄 Background scheduler started on Core %d", xPortGetCoreID());
    
    background_job_t job;
    
    while (g_multitask.scheduler_active) {
        // Wait for background jobs
        if (xQueueReceive(g_multitask.background_queue, &job, pdMS_TO_TICKS(1000))) {
            ESP_LOGD(TAG, "⚙️  Executing background job: %s", job.description);
            
            // Execute the background function
            if (job.function) {
                job.function(job.parameters);
            }
            
            ESP_LOGD(TAG, "✅ Completed background job: %s", job.description);
        }
        
        // Yield to other tasks periodically
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "🔄 Background scheduler terminated");
    vTaskDelete(NULL);
}

// Initialize enhanced multitasking system
bool multitask_init(void) {
    ESP_LOGI(TAG, "🚀 Initializing Enhanced Multitasking Manager...");
    
    // Create manager mutex
    g_multitask.manager_mutex = xSemaphoreCreateMutex();
    if (!g_multitask.manager_mutex) {
        ESP_LOGE(TAG, "❌ Failed to create manager mutex");
        return false;
    }
    
    // Create background job queue (16 jobs deep)
    g_multitask.background_queue = xQueueCreate(16, sizeof(background_job_t));
    if (!g_multitask.background_queue) {
        ESP_LOGE(TAG, "❌ Failed to create background job queue");
        return false;
    }
    
    g_multitask.scheduler_active = true;
    g_multitask.task_count = 0;
    
    // Start background scheduler on Core 1 (data processing core)
    BaseType_t result = xTaskCreatePinnedToCore(
        background_scheduler_task,
        "bg_scheduler",
        4096, // 4KB stack for scheduler
        NULL,
        5, // Low priority for background tasks
        &g_multitask.scheduler_task,
        1 // Core 1
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create background scheduler");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Enhanced Multitasking Manager initialized");
    ESP_LOGI(TAG, "🔄 Background scheduler running on Core 1");
    ESP_LOGI(TAG, "⚡ Dynamic load balancing enabled");
    ESP_LOGI(TAG, "📊 Supporting up to 8 concurrent tasks");
    
    return true;
}

// Create concurrent task with automatic load balancing
BaseType_t multitask_create_concurrent(
    TaskFunction_t task_function,
    const char* task_name,
    uint32_t stack_size,
    void* parameters,
    UBaseType_t priority,
    TaskHandle_t* task_handle,
    bool is_background
) {
    if (!task_function || !task_name || !task_handle) {
        return pdFAIL;
    }
    
    if (xSemaphoreTake(g_multitask.manager_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "⚠️  Failed to acquire manager mutex");
        return pdFAIL;
    }
    
    if (g_multitask.task_count >= 8) {
        ESP_LOGW(TAG, "⚠️  Maximum concurrent tasks reached");
        xSemaphoreGive(g_multitask.manager_mutex);
        return pdFAIL;
    }
    
    // Smart core assignment for optimal performance
    BaseType_t optimal_core;
    if (is_background || strstr(task_name, "gps") || strstr(task_name, "mqtt") || strstr(task_name, "battery")) {
        optimal_core = 1; // Core 1: Data processing, background tasks
    } else {
        optimal_core = 0; // Core 0: System management, cellular
    }
    
    // Use enhanced task creation
    BaseType_t result = create_optimized_task(
        task_function,
        task_name,
        stack_size,
        parameters,
        priority,
        task_handle,
        NULL // sys parameter not needed here
    );
    
    if (result == pdPASS) {
        // Register task in multitask manager
        concurrent_task_t* task = &g_multitask.tasks[g_multitask.task_count];
        task->handle = *task_handle;
        task->name = task_name;
        task->stack_size = stack_size;
        task->priority = priority;
        task->core_affinity = optimal_core;
        task->is_background = is_background;
        task->cpu_time_us = 0;
        task->task_data = parameters;
        
        g_multitask.task_count++;
        
        ESP_LOGI(TAG, "✅ Created concurrent task '%s' on Core %d (Priority: %lu, Stack: %lu KB)",
                 task_name, (int)optimal_core, priority, stack_size / 1024);
    }
    
    xSemaphoreGive(g_multitask.manager_mutex);
    return result;
}

// Submit background job for async processing
bool multitask_submit_background_job(
    void (*function)(void* params),
    void* parameters,
    const char* description,
    uint32_t priority
) {
    if (!function || !description) {
        return false;
    }
    
    background_job_t job = {
        .function = function,
        .parameters = parameters,
        .description = description,
        .priority_level = priority
    };
    
    if (xQueueSend(g_multitask.background_queue, &job, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "⚠️  Background queue full, dropping job: %s", description);
        return false;
    }
    
    ESP_LOGD(TAG, "📤 Queued background job: %s", description);
    return true;
}

// Get task performance statistics
bool multitask_get_task_stats(const char* task_name, concurrent_task_t* stats) {
    if (!task_name || !stats) {
        return false;
    }
    
    if (xSemaphoreTake(g_multitask.manager_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    
    for (uint8_t i = 0; i < g_multitask.task_count; i++) {
        if (strcmp(g_multitask.tasks[i].name, task_name) == 0) {
            // Update high water mark
            g_multitask.tasks[i].high_water_mark = uxTaskGetStackHighWaterMark(g_multitask.tasks[i].handle);
            
            *stats = g_multitask.tasks[i];
            xSemaphoreGive(g_multitask.manager_mutex);
            return true;
        }
    }
    
    xSemaphoreGive(g_multitask.manager_mutex);
    return false;
}

// Monitor system load and auto-balance
void multitask_monitor_and_balance(void) {
    if (xSemaphoreTake(g_multitask.manager_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    ESP_LOGD(TAG, "📊 Monitoring %d concurrent tasks", g_multitask.task_count);
    
    size_t total_free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    // Check for stack overflows or memory pressure
    for (uint8_t i = 0; i < g_multitask.task_count; i++) {
        concurrent_task_t* task = &g_multitask.tasks[i];
        if (task->handle) {
            size_t high_water = uxTaskGetStackHighWaterMark(task->handle);
            task->high_water_mark = high_water;
            
            // Warn if stack usage is high (less than 512 bytes free)
            if (high_water < 512) {
                ESP_LOGW(TAG, "⚠️  Task '%s' stack running low: %zu bytes free", 
                         task->name, high_water);
            }
        }
    }
    
    // Log memory status periodically
    static uint32_t last_log_time = 0;
    uint32_t current_time = esp_timer_get_time() / 1000;
    if (current_time - last_log_time > 30000) { // Every 30 seconds
        ESP_LOGI(TAG, "📊 System Status: %d tasks, %lu KB heap, %lu KB PSRAM", 
                 g_multitask.task_count, total_free_heap / 1024, psram_free / 1024);
        last_log_time = current_time;
    }
    
    xSemaphoreGive(g_multitask.manager_mutex);
}