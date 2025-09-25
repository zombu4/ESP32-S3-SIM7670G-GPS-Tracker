#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../config.h"

// LTE connection status
typedef enum {
    LTE_STATUS_DISCONNECTED = 0,
    LTE_STATUS_CONNECTING,
    LTE_STATUS_CONNECTED,
    LTE_STATUS_ERROR
} lte_status_t;

// LTE network info
typedef struct {
    char operator_name[32];
    char network_type[16];  // 2G, 3G, 4G, etc.
    int signal_strength;    // dBm
    int signal_quality;     // 0-31 or 99 if unknown
    char cell_id[16];
    char location_area[8];
} lte_network_info_t;

// LTE module status
typedef struct {
    bool initialized;
    lte_status_t connection_status;
    lte_network_info_t network_info;
    bool sim_ready;
    bool pdp_active;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t connection_uptime_ms;
    uint32_t last_error_code;
} lte_module_status_t;

// AT command response
typedef struct {
    bool success;
    char response[512];
    uint32_t response_time_ms;
} at_response_t;

// LTE module interface
typedef struct {
    bool (*init)(const lte_config_t* config);
    bool (*deinit)(void);
    bool (*connect)(void);
    bool (*disconnect)(void);
    lte_status_t (*get_connection_status)(void);
    bool (*get_status)(lte_module_status_t* status);
    bool (*get_network_info)(lte_network_info_t* info);
    bool (*send_at_command)(const char* command, at_response_t* response, int timeout_ms);
    bool (*set_apn)(const char* apn, const char* username, const char* password);
    bool (*check_sim_ready)(void);
    bool (*get_signal_strength)(int* rssi, int* quality);
    void (*set_debug)(bool enable);
} lte_interface_t;

// Get LTE module interface
const lte_interface_t* lte_get_interface(void);

// LTE utility functions
const char* lte_status_to_string(lte_status_t status);
bool lte_is_connected(void);
bool lte_format_network_info(const lte_network_info_t* info, char* buffer, size_t buffer_size);