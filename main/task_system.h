#ifndef TASK_SYSTEM_H
#define TASK_SYSTEM_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "config.h"

// External reference to system config (defined in main)
extern tracker_system_config_t system_config;

// Task priorities (higher number = higher priority)
#define TASK_PRIORITY_SYSTEM_MONITOR    (configMAX_PRIORITIES - 1)  // Highest
#define TASK_PRIORITY_CELLULAR          (configMAX_PRIORITIES - 2)  // Critical
#define TASK_PRIORITY_GPS               (configMAX_PRIORITIES - 3)  // High  
#define TASK_PRIORITY_MQTT              (configMAX_PRIORITIES - 4)  // Normal
#define TASK_PRIORITY_BATTERY           (configMAX_PRIORITIES - 5)  // Low

// Stack sizes optimized for ESP32-S3 32-bit mode with 2MB PSRAM
#define TASK_STACK_SIZE_SYSTEM_MONITOR  12288   // 12KB - More space for monitoring
#define TASK_STACK_SIZE_CELLULAR        8192    // 8KB - LTE/AT commands need buffer space  
#define TASK_STACK_SIZE_GPS             6144    // 6KB - NMEA parsing with larger buffers
#define TASK_STACK_SIZE_MQTT            10240   // 10KB - JSON payload handling
#define TASK_STACK_SIZE_BATTERY         4096    // 4KB - Simple I2C operations

// Event bits for task coordination
#define EVENT_CELLULAR_READY       BIT0
#define EVENT_GPS_FIX_ACQUIRED     BIT1
#define EVENT_GPS_DATA_FRESH       BIT2
#define EVENT_MQTT_READY           BIT3
#define EVENT_BATTERY_DATA_READY   BIT4
#define EVENT_SYSTEM_SHUTDOWN      BIT5
#define EVENT_CELLULAR_LOST        BIT6
#define EVENT_GPS_FIX_LOST        BIT7
#define EVENT_MQTT_DISCONNECTED    BIT8

// Task states
typedef enum {
    TASK_STATE_INIT,
    TASK_STATE_RUNNING,
    TASK_STATE_READY,
    TASK_STATE_ERROR,
    TASK_STATE_SHUTDOWN
} task_state_t;

// CPU affinity options for dynamic dual-core load balancing
typedef enum {
    CPU_AFFINITY_AUTO = tskNO_AFFINITY,     // Let system decide (dynamic)
    CPU_AFFINITY_CORE0 = 0,                 // Pin to Core 0 (Protocol/WiFi)
    CPU_AFFINITY_CORE1 = 1,                 // Pin to Core 1 (Application)
    CPU_AFFINITY_BALANCED                   // Special: Load balance automatically
} cpu_affinity_t;

// Memory allocation preferences for 32-bit PSRAM optimization
typedef enum {
    MEM_ALLOC_INTERNAL,     // Force internal RAM (DMA-capable)
    MEM_ALLOC_EXTERNAL,     // Prefer external PSRAM
    MEM_ALLOC_BALANCED,     // Auto balance based on size
    MEM_ALLOC_CACHE_AWARE   // Optimize for cache performance
} memory_allocation_type_t;

// Enhanced task information for 32-bit dual-core optimization
typedef struct {
    TaskHandle_t handle;
    task_state_t state;
    cpu_affinity_t preferred_cpu;
    cpu_affinity_t current_cpu;
    uint32_t stack_high_water_mark;
    uint32_t stack_size_bytes;
    uint32_t cpu_usage_percent;
    uint32_t memory_usage_bytes;
    uint32_t last_heartbeat_ms;
    uint32_t execution_time_us;
    bool auto_recovery_enabled;
    bool dynamic_affinity_enabled;
    bool external_memory_enabled;
    memory_allocation_type_t memory_preference;
    const char* name;
    
    // Performance counters for dual-core optimization
    uint32_t core_switches;
    uint32_t cache_misses;
    uint32_t memory_allocations;
} task_info_t;

// Dual-core load balancer state
typedef struct {
    uint32_t core0_load_percent;
    uint32_t core1_load_percent;
    uint32_t core0_free_stack;
    uint32_t core1_free_stack;
    uint32_t total_psram_used;
    uint32_t total_internal_used;
    bool load_balancing_active;
} dual_core_state_t;

// Enhanced 32-bit memory manager
typedef struct {
    uint32_t internal_heap_free;
    uint32_t external_heap_free;
    uint32_t largest_internal_block;
    uint32_t largest_external_block;
    uint32_t cache_hit_ratio_percent;
    uint32_t fragmentation_percent;
    bool external_memory_optimized;
} memory_manager_state_t;

// System-wide task management with 32-bit dual-core optimization
typedef struct {
    // Task handles and info
    task_info_t cellular_task;
    task_info_t gps_task;
    task_info_t mqtt_task;
    task_info_t battery_task;
    task_info_t system_monitor_task;
    
    // Dual-core optimization
    dual_core_state_t core_state;
    memory_manager_state_t memory_state;
    
    // Synchronization primitives
    EventGroupHandle_t system_events;
    QueueHandle_t cellular_queue;
    QueueHandle_t gps_queue;
    QueueHandle_t mqtt_queue;
    QueueHandle_t battery_queue;
    SemaphoreHandle_t system_mutex;
    
    // System status
    bool system_initialized;
    bool system_running;
    uint32_t system_start_time_ms;
    uint32_t total_runtime_ms;
    
    // CPU load balancing
    bool dynamic_affinity_enabled;
    uint32_t core0_load_percent;
    uint32_t core1_load_percent;
    
    // Error tracking
    uint32_t cellular_error_count;
    uint32_t gps_error_count;
    uint32_t mqtt_error_count;
    uint32_t battery_error_count;
    
} task_system_t;

// Task system interface
typedef struct {
    bool (*init)(void);
    bool (*start_all_tasks)(void);
    bool (*stop_all_tasks)(void);
    bool (*restart_task)(const char* task_name);
    task_state_t (*get_task_state)(const char* task_name);
    bool (*set_cpu_affinity)(const char* task_name, cpu_affinity_t affinity);
    uint32_t (*get_cpu_usage)(const char* task_name);
    uint32_t (*get_stack_usage)(const char* task_name);
    bool (*wait_for_system_ready)(uint32_t timeout_ms);
    bool (*is_system_healthy)(void);
    void (*print_system_status)(void);
    void (*enable_auto_recovery)(bool enable);
    void (*enable_dynamic_affinity)(bool enable);
} task_system_interface_t;

// Message types for inter-task communication
typedef enum {
    MSG_TYPE_DATA,
    MSG_TYPE_COMMAND,
    MSG_TYPE_STATUS,
    MSG_TYPE_ERROR
} msg_type_t;

// Generic message structure
typedef struct {
    msg_type_t type;
    uint32_t timestamp_ms;
    size_t data_length;
    void* data;
} task_message_t;

// Cellular task specific messages
typedef enum {
    CELLULAR_CMD_INIT,
    CELLULAR_CMD_CONNECT,
    CELLULAR_CMD_DISCONNECT,
    CELLULAR_CMD_CHECK_SIGNAL,
    CELLULAR_CMD_RESET_MODEM
} cellular_cmd_t;

// GPS task specific messages  
typedef enum {
    GPS_CMD_START,
    GPS_CMD_STOP,
    GPS_CMD_POLL_LOCATION,
    GPS_CMD_RESET_MODULE
} gps_cmd_t;

// MQTT task specific messages
typedef enum {
    MQTT_CMD_CONNECT,
    MQTT_CMD_DISCONNECT,
    MQTT_CMD_PUBLISH,
    MQTT_CMD_SUBSCRIBE,
    MQTT_CMD_RESET_CLIENT
} mqtt_cmd_t;

// Public interface
extern const task_system_interface_t* get_task_system_interface(void);

// Task entry points (implemented in separate task files)
extern void cellular_task_entry(void* parameters);
extern void gps_task_entry(void* parameters);
extern void mqtt_task_entry(void* parameters);
extern void battery_task_entry(void* parameters);  
extern void system_monitor_task_entry(void* parameters);

// Utility functions
uint32_t get_current_timestamp_ms(void);
void update_task_heartbeat(const char* task_name);
bool send_task_message(QueueHandle_t queue, msg_type_t type, void* data, size_t data_size);
bool receive_task_message(QueueHandle_t queue, task_message_t* message, uint32_t timeout_ms);
void balance_cpu_load(void);
void monitor_task_health(void);

#endif // TASK_SYSTEM_H