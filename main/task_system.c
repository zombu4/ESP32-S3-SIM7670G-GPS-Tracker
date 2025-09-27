#include "task_system.h"
#include "dual_core_manager.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "modules/lte/lte_module.h"
#include "modules/gps/gps_module.h"
#include "modules/mqtt/mqtt_module.h"
#include "modules/battery/battery_module.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

static const char *TAG = "TASK_SYSTEM";

// Global task system state
static task_system_t task_system = {0};

// Forward declarations
static bool task_system_init_impl(void);
static bool task_system_start_all_tasks_impl(void);
static bool task_system_stop_all_tasks_impl(void);
static bool task_system_restart_task_impl(const char* task_name);
static task_state_t task_system_get_task_state_impl(const char* task_name);
static bool task_system_set_cpu_affinity_impl(const char* task_name, cpu_affinity_t affinity);
static uint32_t task_system_get_cpu_usage_impl(const char* task_name);
static uint32_t task_system_get_stack_usage_impl(const char* task_name);
static bool task_system_wait_for_system_ready_impl(uint32_t timeout_ms);
static bool task_system_is_system_healthy_impl(void);
static void task_system_print_system_status_impl(void);
static void task_system_enable_auto_recovery_impl(bool enable);
static void task_system_enable_dynamic_affinity_impl(bool enable);

// Task system interface
static const task_system_interface_t task_system_interface = {
    .init = task_system_init_impl,
    .start_all_tasks = task_system_start_all_tasks_impl,
    .stop_all_tasks = task_system_stop_all_tasks_impl,
    .restart_task = task_system_restart_task_impl,
    .get_task_state = task_system_get_task_state_impl,
    .set_cpu_affinity = task_system_set_cpu_affinity_impl,
    .get_cpu_usage = task_system_get_cpu_usage_impl,
    .get_stack_usage = task_system_get_stack_usage_impl,
    .wait_for_system_ready = task_system_wait_for_system_ready_impl,
    .is_system_healthy = task_system_is_system_healthy_impl,
    .print_system_status = task_system_print_system_status_impl,
    .enable_auto_recovery = task_system_enable_auto_recovery_impl,
    .enable_dynamic_affinity = task_system_enable_dynamic_affinity_impl
};

const task_system_interface_t* get_task_system_interface(void) {
    return &task_system_interface;
}

static bool task_system_init_impl(void) {
    if (task_system.system_initialized) {
        ESP_LOGW(TAG, "Task system already initialized");
        return true;
    }

    ESP_LOGI(TAG, "🚀 Initializing Task System for ESP32-S3 Dual Core");
    
    // Create system synchronization primitives
    task_system.system_events = xEventGroupCreate();
    if (!task_system.system_events) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return false;
    }

    task_system.system_mutex = xSemaphoreCreateMutex();
    if (!task_system.system_mutex) {
        ESP_LOGE(TAG, "Failed to create system mutex");
        return false;
    }

    // Create inter-task communication queues
    task_system.cellular_queue = xQueueCreate(10, sizeof(task_message_t));
    task_system.gps_queue = xQueueCreate(10, sizeof(task_message_t));
    task_system.mqtt_queue = xQueueCreate(20, sizeof(task_message_t));
    task_system.battery_queue = xQueueCreate(5, sizeof(task_message_t));

    if (!task_system.cellular_queue || !task_system.gps_queue || 
        !task_system.mqtt_queue || !task_system.battery_queue) {
        ESP_LOGE(TAG, "Failed to create task queues");
        return false;
    }

    // Initialize task info structures
    task_system.cellular_task.name = "cellular";
    task_system.gps_task.name = "gps";
    task_system.mqtt_task.name = "mqtt";
    task_system.battery_task.name = "battery";
    task_system.system_monitor_task.name = "monitor";

    // Set initial states and initialize task info
    task_system.cellular_task.state = TASK_STATE_INIT;
    task_system.cellular_task.handle = NULL;
    task_system.cellular_task.current_cpu = CPU_AFFINITY_AUTO;
    task_system.cellular_task.auto_recovery_enabled = true;
    
    task_system.gps_task.state = TASK_STATE_INIT;
    task_system.gps_task.handle = NULL;
    task_system.gps_task.current_cpu = CPU_AFFINITY_AUTO;
    task_system.gps_task.auto_recovery_enabled = true;
    
    task_system.mqtt_task.state = TASK_STATE_INIT;
    task_system.mqtt_task.handle = NULL;
    task_system.mqtt_task.current_cpu = CPU_AFFINITY_AUTO;
    task_system.mqtt_task.auto_recovery_enabled = true;
    
    task_system.battery_task.state = TASK_STATE_INIT;
    task_system.battery_task.handle = NULL;
    task_system.battery_task.current_cpu = CPU_AFFINITY_AUTO;
    task_system.battery_task.auto_recovery_enabled = true;
    
    task_system.system_monitor_task.state = TASK_STATE_INIT;
    task_system.system_monitor_task.handle = NULL;
    task_system.system_monitor_task.current_cpu = CPU_AFFINITY_AUTO;
    task_system.system_monitor_task.auto_recovery_enabled = true;

    // Enable dynamic CPU affinity by default
    task_system.dynamic_affinity_enabled = true;
    
    task_system.system_start_time_ms = get_current_timestamp_ms();
    task_system.system_initialized = true;

    ESP_LOGI(TAG, "✅ Task System initialized successfully");
    ESP_LOGI(TAG, "📊 Queue sizes - Cellular:10, GPS:10, MQTT:20, Battery:5");
    ESP_LOGI(TAG, "🔄 Dynamic CPU affinity enabled for dual-core optimization");
    
    return true;
}

static bool task_system_start_all_tasks_impl(void) {
    if (!task_system.system_initialized) {
        ESP_LOGE(TAG, "Task system not initialized");
        return false;
    }

    if (task_system.system_running) {
        ESP_LOGW(TAG, "Task system already running");
        return true;
    }

    ESP_LOGI(TAG, "🎯 Starting all system tasks with optimized 32-bit dual-core architecture...");

    // **CRITICAL FIX**: Set system_running to true BEFORE creating tasks to avoid race condition
    ESP_LOGW(TAG, "🔧 RACE CONDITION FIX: Setting system_running=TRUE BEFORE task creation");
    task_system.system_running = true;
    ESP_LOGI(TAG, "✅ System running flag set to: %s", task_system.system_running ? "TRUE" : "FALSE");

    // Initialize dual-core manager
    task_system_init_dual_core_manager(&task_system);

    ESP_LOGI(TAG, "📊 Current system_running state BEFORE task creation: %s", 
             task_system.system_running ? "TRUE" : "FALSE");
    ESP_LOGI(TAG, "📊 Task system address being passed to tasks: %p", &task_system);
    
    // Start system monitor first (highest priority) - Core 0 for system management
    ESP_LOGI(TAG, "🔄 Creating system monitor task...");
    BaseType_t result = create_optimized_task(
        system_monitor_task_entry,
        "sys_monitor",
        TASK_STACK_SIZE_SYSTEM_MONITOR,
        &task_system,
        TASK_PRIORITY_SYSTEM_MONITOR,
        &task_system.system_monitor_task.handle,
        &task_system
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system monitor task");
        return false;
    }
    
    // Start cellular task (critical - manages network connectivity) - Core 0 for protocol stack
    ESP_LOGI(TAG, "🔄 Creating cellular task...");
    result = create_optimized_task(
        cellular_task_entry,
        "cellular",
        TASK_STACK_SIZE_CELLULAR,
        &task_system,
        TASK_PRIORITY_CELLULAR,
        &task_system.cellular_task.handle,
        &task_system
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cellular task");
        return false;
    }

    // Start GPS task (high priority - location data) - Core 1 for data processing
    result = create_optimized_task(
        gps_task_entry,
        "gps",
        TASK_STACK_SIZE_GPS,
        &task_system,
        TASK_PRIORITY_GPS,
        &task_system.gps_task.handle,
        &task_system
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
        return false;
    }

    // Start battery monitoring task (lowest priority) - Core 1 for I2C processing
    result = create_optimized_task(
        battery_task_entry,
        "battery",
        TASK_STACK_SIZE_BATTERY,
        &task_system,
        TASK_PRIORITY_BATTERY,
        &task_system.battery_task.handle,
        &task_system
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery task");
        return false;
    }

    // MQTT task will be started by system monitor when conditions are met
    ESP_LOGI(TAG, "📱 MQTT task will start when cellular + GPS conditions are ready");

    ESP_LOGI(TAG, "🔄 System running flag already set, confirming: %s", 
             task_system.system_running ? "TRUE" : "FALSE");
    
    ESP_LOGI(TAG, "✅ All initial tasks started successfully");
    ESP_LOGI(TAG, "⚡ System running on ESP32-S3 dual-core with task priorities:");
    ESP_LOGI(TAG, "   📡 Cellular: Priority %d", TASK_PRIORITY_CELLULAR);
    ESP_LOGI(TAG, "   🛰️  GPS: Priority %d", TASK_PRIORITY_GPS);
    ESP_LOGI(TAG, "   📊 Monitor: Priority %d", TASK_PRIORITY_SYSTEM_MONITOR);
    ESP_LOGI(TAG, "   🔋 Battery: Priority %d", TASK_PRIORITY_BATTERY);
    ESP_LOGI(TAG, "   📨 MQTT: Priority %d (will start when ready)", TASK_PRIORITY_MQTT);

    return true;
}

// System Monitor Task - Oversees all other tasks and starts MQTT when ready
void system_monitor_task_entry(void* parameters) {
    task_system_t* sys = (task_system_t*)parameters;
    
    ESP_LOGI(TAG, "🔍 System Monitor Task started on Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "🔍 Monitor received parameters: %p", parameters);
    ESP_LOGI(TAG, "🔍 System pointer: %p", sys);
    ESP_LOGI(TAG, "🔍 System running flag at start: %s", sys ? (sys->system_running ? "TRUE" : "FALSE") : "NULL_SYS");
    
    // Register with watchdog
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "🔍 Monitor registered with watchdog");
    
    if (sys) {
        sys->system_monitor_task.state = TASK_STATE_RUNNING;
        sys->system_monitor_task.current_cpu = xPortGetCoreID();
        ESP_LOGI(TAG, "🔍 Monitor state set to RUNNING on Core %d", sys->system_monitor_task.current_cpu);
    } else {
        ESP_LOGE(TAG, "❌ Monitor task received NULL system pointer!");
    }
    
    bool mqtt_task_started = false;
    uint32_t mqtt_start_check_interval = 5000; // Check every 5 seconds
    uint32_t last_mqtt_check = 0;
    uint32_t loop_count = 0;
    
    ESP_LOGI(TAG, "🔍 Monitor entering main loop - system_running: %s", 
             sys->system_running ? "TRUE" : "FALSE");
    
    while (sys->system_running) {
        loop_count++;
        esp_task_wdt_reset();
        update_task_heartbeat("monitor");
        
        uint32_t current_time = get_current_timestamp_ms();
        
        // Log every 10 iterations for verbose monitoring
        if (loop_count % 10 == 0) {
            ESP_LOGI(TAG, "🔍 Monitor loop #%lu - system_running: %s, time: %lu", 
                     loop_count, sys->system_running ? "TRUE" : "FALSE", current_time);
        }
        
        // Check if conditions are met to start MQTT task
        if (!mqtt_task_started && (current_time - last_mqtt_check) >= mqtt_start_check_interval) {
            last_mqtt_check = current_time;
            
            // Wait for cellular ready event
            EventBits_t cellular_bits = xEventGroupWaitBits(
                sys->system_events,
                EVENT_CELLULAR_READY,
                pdFALSE,  // Don't clear
                pdTRUE,   // Wait for all bits
                0         // No wait
            );
            
            // Check for GPS module ready (not necessarily fix - module can be ready without satellite fix)
            bool gps_module_ready = (sys->gps_task.state == TASK_STATE_READY || sys->gps_task.state == TASK_STATE_RUNNING);
            
            if ((cellular_bits & EVENT_CELLULAR_READY) && gps_module_ready) {
                ESP_LOGI(TAG, "🎯 CONDITIONS MET: Starting MQTT task now!");
                ESP_LOGI(TAG, "   ✅ Cellular connection: READY");
                ESP_LOGI(TAG, "   ✅ GPS module: READY (fix not required for MQTT start)");
                
                BaseType_t result = create_optimized_task(
                    mqtt_task_entry,
                    "mqtt",
                    TASK_STACK_SIZE_MQTT,
                    sys,
                    TASK_PRIORITY_MQTT,
                    &sys->mqtt_task.handle,
                    sys
                );
                
                if (result == pdPASS) {
                    mqtt_task_started = true;
                    sys->mqtt_task.state = TASK_STATE_RUNNING;
                    ESP_LOGI(TAG, "📨 MQTT task started successfully on Core %d", xPortGetCoreID());
                } else {
                    ESP_LOGE(TAG, "❌ Failed to start MQTT task");
                }
            } else {
                // Log what we're still waiting for
                if (!(cellular_bits & EVENT_CELLULAR_READY)) {
                    ESP_LOGI(TAG, "⏳ Waiting for cellular connection...");
                }
                if (!gps_module_ready) {
                    ESP_LOGI(TAG, "⏳ Waiting for GPS module initialization...");
                }
            }
        }
        
        // Monitor task health and CPU load
        monitor_task_health();
        
        // Update dual-core performance counters and load balancing
        task_system_update_performance_counters(sys);
        
        // Dynamic CPU load balancing if enabled
        if (sys->dynamic_affinity_enabled) {
            balance_cpu_load();
        }
        
        // Update system runtime
        sys->total_runtime_ms = current_time - sys->system_start_time_ms;
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Monitor every 1 second
    }
    
    esp_task_wdt_delete(NULL);
    sys->system_monitor_task.state = TASK_STATE_SHUTDOWN;
    vTaskDelete(NULL);
}

// Utility functions
uint32_t get_current_timestamp_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void update_task_heartbeat(const char* task_name) {
    uint32_t current_time = get_current_timestamp_ms();
    
    if (strcmp(task_name, "cellular") == 0) {
        task_system.cellular_task.last_heartbeat_ms = current_time;
    } else if (strcmp(task_name, "gps") == 0) {
        task_system.gps_task.last_heartbeat_ms = current_time;
    } else if (strcmp(task_name, "mqtt") == 0) {
        task_system.mqtt_task.last_heartbeat_ms = current_time;
    } else if (strcmp(task_name, "battery") == 0) {
        task_system.battery_task.last_heartbeat_ms = current_time;
    } else if (strcmp(task_name, "monitor") == 0) {
        task_system.system_monitor_task.last_heartbeat_ms = current_time;
    }
}

bool send_task_message(QueueHandle_t queue, msg_type_t type, void* data, size_t data_size) {
    if (!queue) return false;
    
    task_message_t message = {
        .type = type,
        .timestamp_ms = get_current_timestamp_ms(),
        .data_length = data_size,
        .data = data
    };
    
    return xQueueSend(queue, &message, pdMS_TO_TICKS(1000)) == pdPASS;
}

bool receive_task_message(QueueHandle_t queue, task_message_t* message, uint32_t timeout_ms) {
    if (!queue || !message) return false;
    
    return xQueueReceive(queue, message, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

void balance_cpu_load(void) {
    // TODO: Implement dynamic CPU load balancing based on task CPU usage
    // This could migrate tasks between cores based on load
}

void monitor_task_health(void) {
    uint32_t current_time = get_current_timestamp_ms();
    const uint32_t HEARTBEAT_TIMEOUT = 30000; // 30 seconds
    
    // Check for task timeouts and update stack usage
    if (task_system.cellular_task.handle) {
        task_system.cellular_task.stack_high_water_mark = uxTaskGetStackHighWaterMark(task_system.cellular_task.handle);
        
        if ((current_time - task_system.cellular_task.last_heartbeat_ms) > HEARTBEAT_TIMEOUT) {
            ESP_LOGW(TAG, "⚠️  Cellular task heartbeat timeout detected");
            task_system.cellular_error_count++;
        }
    }
    
    if (task_system.gps_task.handle) {
        task_system.gps_task.stack_high_water_mark = uxTaskGetStackHighWaterMark(task_system.gps_task.handle);
        
        if ((current_time - task_system.gps_task.last_heartbeat_ms) > HEARTBEAT_TIMEOUT) {
            ESP_LOGW(TAG, "⚠️  GPS task heartbeat timeout detected");
            task_system.gps_error_count++;
        }
    }
    
    if (task_system.mqtt_task.handle) {
        task_system.mqtt_task.stack_high_water_mark = uxTaskGetStackHighWaterMark(task_system.mqtt_task.handle);
        
        if ((current_time - task_system.mqtt_task.last_heartbeat_ms) > HEARTBEAT_TIMEOUT) {
            ESP_LOGW(TAG, "⚠️  MQTT task heartbeat timeout detected");
            task_system.mqtt_error_count++;
        }
    }
    
    if (task_system.battery_task.handle) {
        task_system.battery_task.stack_high_water_mark = uxTaskGetStackHighWaterMark(task_system.battery_task.handle);
        
        if ((current_time - task_system.battery_task.last_heartbeat_ms) > HEARTBEAT_TIMEOUT) {
            ESP_LOGW(TAG, "⚠️  Battery task heartbeat timeout detected");
            task_system.battery_error_count++;
        }
    }
}

// Placeholder implementations for interface functions
static bool task_system_stop_all_tasks_impl(void) {
    task_system.system_running = false;
    return true;
}

static bool task_system_restart_task_impl(const char* task_name) {
    // TODO: Implement task restart logic
    return false;
}

static task_state_t task_system_get_task_state_impl(const char* task_name) {
    if (strcmp(task_name, "cellular") == 0) return task_system.cellular_task.state;
    if (strcmp(task_name, "gps") == 0) return task_system.gps_task.state;
    if (strcmp(task_name, "mqtt") == 0) return task_system.mqtt_task.state;
    if (strcmp(task_name, "battery") == 0) return task_system.battery_task.state;
    if (strcmp(task_name, "monitor") == 0) return task_system.system_monitor_task.state;
    return TASK_STATE_ERROR;
}

static bool task_system_set_cpu_affinity_impl(const char* task_name, cpu_affinity_t affinity) {
    // TODO: Implement CPU affinity changes for running tasks
    return false;
}

static uint32_t task_system_get_cpu_usage_impl(const char* task_name) {
    // TODO: Implement CPU usage tracking
    return 0;
}

static uint32_t task_system_get_stack_usage_impl(const char* task_name) {
    if (strcmp(task_name, "cellular") == 0) return task_system.cellular_task.stack_high_water_mark;
    if (strcmp(task_name, "gps") == 0) return task_system.gps_task.stack_high_water_mark;
    if (strcmp(task_name, "mqtt") == 0) return task_system.mqtt_task.stack_high_water_mark;
    if (strcmp(task_name, "battery") == 0) return task_system.battery_task.stack_high_water_mark;
    if (strcmp(task_name, "monitor") == 0) return task_system.system_monitor_task.stack_high_water_mark;
    return 0;
}

static bool task_system_wait_for_system_ready_impl(uint32_t timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(
        task_system.system_events,
        EVENT_CELLULAR_READY | EVENT_GPS_FIX_ACQUIRED | EVENT_MQTT_READY,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(timeout_ms)
    );
    
    return (bits & (EVENT_CELLULAR_READY | EVENT_GPS_FIX_ACQUIRED | EVENT_MQTT_READY)) == 
           (EVENT_CELLULAR_READY | EVENT_GPS_FIX_ACQUIRED | EVENT_MQTT_READY);
}

static bool task_system_is_system_healthy_impl(void) {
    return task_system.system_running &&
           (task_system.cellular_error_count < 10) &&
           (task_system.gps_error_count < 10) &&
           (task_system.mqtt_error_count < 10);
}

static void task_system_print_system_status_impl(void) {
    ESP_LOGI(TAG, "📊 TASK SYSTEM STATUS");
    ESP_LOGI(TAG, "   Runtime: %lu ms", task_system.total_runtime_ms);
    ESP_LOGI(TAG, "   Cellular errors: %lu", task_system.cellular_error_count);
    ESP_LOGI(TAG, "   GPS errors: %lu", task_system.gps_error_count);
    ESP_LOGI(TAG, "   MQTT errors: %lu", task_system.mqtt_error_count);
    ESP_LOGI(TAG, "   Battery errors: %lu", task_system.battery_error_count);
}

static void task_system_enable_auto_recovery_impl(bool enable) {
    task_system.cellular_task.auto_recovery_enabled = enable;
    task_system.gps_task.auto_recovery_enabled = enable;
    task_system.mqtt_task.auto_recovery_enabled = enable;
    task_system.battery_task.auto_recovery_enabled = enable;
}

static void task_system_enable_dynamic_affinity_impl(bool enable) {
    task_system.dynamic_affinity_enabled = enable;
}