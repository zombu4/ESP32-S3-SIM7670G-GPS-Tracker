#ifndef APN_MANAGER_H
#define APN_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// APN Configuration Structure
typedef struct {
    char apn[64];           // APN name (e.g., "m2mglobal")
    char username[32];      // APN username (optional)
    char password[32];      // APN password (optional)
    bool auto_detect;       // Auto-detect APN from SIM
    bool persistence;       // Save APN to NVS
    bool debug;             // Enable debug logging
} apn_config_t;

// APN Status Structure
typedef struct {
    bool is_configured;     // APN is configured on modem
    bool is_active;         // PDP context is active
    char current_apn[64];   // Currently configured APN
    char ip_address[16];    // Assigned IP address
    uint32_t config_time;   // Timestamp when APN was configured
} apn_status_t;

// APN Manager Interface
typedef struct apn_manager_interface {
    // Core functions
    bool (*init)(const apn_config_t* config);
    bool (*deinit)(void);
    
    // APN management
    bool (*check_configuration)(apn_status_t* status);
    bool (*set_apn)(const char* apn, const char* username, const char* password);
    bool (*activate_context)(void);
    bool (*deactivate_context)(void);
    
    // Query functions
    bool (*get_status)(apn_status_t* status);
    bool (*is_ready_for_data)(void);
    
    // Utility functions
    bool (*save_to_nvs)(void);
    bool (*load_from_nvs)(void);
    void (*set_debug)(bool enable);
} apn_manager_interface_t;

// Get APN Manager Interface
const apn_manager_interface_t* apn_manager_get_interface(void);

// Default APN Configuration
extern const apn_config_t APN_CONFIG_DEFAULT;

#ifdef __cplusplus
}
#endif

#endif // APN_MANAGER_H