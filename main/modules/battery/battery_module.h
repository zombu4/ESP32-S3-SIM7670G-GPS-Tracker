#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../config.h"

// Battery data structure
typedef struct {
 float percentage;
 float voltage;
 bool charging;
 bool present;
 float charge_rate; // mA (positive = charging, negative = discharging)
 uint32_t charge_cycles;
 float temperature; // Celsius (if supported)
} battery_data_t;

// Battery status
typedef struct {
 bool initialized;
 bool sensor_ready;
 uint32_t last_read_time;
 uint32_t total_reads;
 uint32_t read_errors;
 bool low_battery_alert;
 bool critical_battery_alert;
} battery_status_t;

// Battery module interface
typedef struct {
 bool (*init)(const battery_config_t* config);
 bool (*deinit)(void);
 bool (*read_data)(battery_data_t* data);
 bool (*get_status)(battery_status_t* status);
 bool (*calibrate)(void);
 bool (*reset)(void);
 void (*set_debug)(bool enable);
} battery_interface_t;

// Get battery module interface
const battery_interface_t* battery_get_interface(void);

// Battery utility functions
bool battery_is_low(const battery_data_t* data, float threshold);
bool battery_is_critical(const battery_data_t* data, float threshold);
const char* battery_get_status_string(const battery_data_t* data);
bool battery_format_info(const battery_data_t* data, char* buffer, size_t buffer_size);