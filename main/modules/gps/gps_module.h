#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../config.h"

// GPS data structure
typedef struct {
    float latitude;
    float longitude;
    float altitude;
    float speed_kmh;
    float course;
    int satellites;
    bool fix_valid;
    char timestamp[32];
    float hdop;  // Horizontal dilution of precision
    char fix_quality;  // GPS fix quality indicator
} gps_data_t;

// GPS module status
typedef struct {
    bool initialized;
    bool uart_ready;
    bool gps_power_on;
    uint32_t last_fix_time;
    uint32_t total_sentences_parsed;
    uint32_t valid_sentences;
    uint32_t parse_errors;
} gps_status_t;

// GPS module interface
typedef struct {
    bool (*init)(const gps_config_t* config);
    bool (*deinit)(void);
    bool (*read_data)(gps_data_t* data);
    bool (*get_status)(gps_status_t* status);
    bool (*power_on)(void);
    bool (*power_off)(void);
    bool (*reset)(void);
    void (*set_debug)(bool enable);
} gps_interface_t;

// Get GPS module interface
const gps_interface_t* gps_get_interface(void);

// GPS utility functions
bool gps_is_fix_valid(const gps_data_t* data);
float gps_calculate_distance(float lat1, float lon1, float lat2, float lon2);
bool gps_format_coordinates(const gps_data_t* data, char* buffer, size_t buffer_size);