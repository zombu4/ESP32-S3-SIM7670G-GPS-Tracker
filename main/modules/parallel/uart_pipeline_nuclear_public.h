/**
 * 🔥💀 UART PIPELINE NUCLEAR PUBLIC INTERFACE 💀🔥
 * 
 * Public API for the nuclear UART pipeline system
 * Provides high-performance dual-core AT command routing
 */

#ifndef UART_PIPELINE_NUCLEAR_PUBLIC_H
#define UART_PIPELINE_NUCLEAR_PUBLIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Nuclear UART pipeline configuration
typedef struct {
    uint32_t buffer_size;
    uint32_t timeout_ms;
    bool enable_debug;
} nuclear_uart_config_t;

// Nuclear UART pipeline interface
typedef struct {
    esp_err_t (*init)(const nuclear_uart_config_t* config);
    esp_err_t (*send_command)(const char* command, char* response, size_t response_size, uint32_t timeout_ms);
    esp_err_t (*deinit)(void);
    void (*get_debug_info)(char* debug_str, size_t max_len);
} nuclear_uart_interface_t;

/**
 * Get the nuclear UART pipeline interface
 * @return Pointer to nuclear UART interface structure
 */
const nuclear_uart_interface_t* get_nuclear_uart_pipeline_interface(void);

/**
 * Read GPS data from nuclear pipeline
 * @param buffer Buffer to store GPS data
 * @param buffer_size Size of buffer
 * @param bytes_read Pointer to store number of bytes read
 * @return true if data read successfully, false otherwise
 */
bool nuclear_pipeline_read_gps_data(uint8_t* buffer, size_t buffer_size, size_t* bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* UART_PIPELINE_NUCLEAR_PUBLIC_H */