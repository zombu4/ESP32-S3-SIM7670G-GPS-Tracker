#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// GPS Debug Module - Separate from core GPS functionality
// Can be easily removed or disabled without affecting GPS operations

typedef struct {
    bool enable_verbose_uart;
    bool enable_hex_dumps;
    bool enable_nmea_analysis;
    bool enable_timing_logs;
    bool enable_command_tracking;
} gps_debug_config_t;

// Debug interface
typedef struct {
    bool (*init)(const gps_debug_config_t* config);
    void (*deinit)(void);
    void (*log_uart_read_attempt)(int attempt, int total_attempts);
    void (*log_uart_read_result)(bool success, size_t bytes_read);
    void (*log_uart_data)(const char* buffer, size_t size);
    void (*log_hex_dump)(const char* buffer, size_t size);
    void (*log_nmea_analysis)(const char* buffer, size_t size);
    void (*log_at_command)(const char* command, const char* response);
    void (*set_verbose_level)(int level);
} gps_debug_interface_t;

// Get debug interface
const gps_debug_interface_t* gps_debug_get_interface(void);

#ifdef __cplusplus
}
#endif