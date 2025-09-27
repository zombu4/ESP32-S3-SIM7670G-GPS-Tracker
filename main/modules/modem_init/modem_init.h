#ifndef MODEM_INIT_H
#define MODEM_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modem initialization status
 */
typedef enum {
 MODEM_STATUS_UNKNOWN = 0,
 MODEM_STATUS_READY,
 MODEM_STATUS_SIM_READY,
 MODEM_STATUS_NETWORK_REGISTERED,
 MODEM_STATUS_DATA_CONNECTED,
 MODEM_STATUS_FAILED
} modem_status_t;

/**
 * @brief Network connectivity test result
 */
typedef struct {
 bool ping_success;
 int response_time_ms;
 char error_message[128];
} network_test_result_t;

/**
 * @brief GPS fix information
 */
typedef struct {
 bool has_fix;
 float latitude;
 float longitude;
 float altitude;
 int satellites_used;
 char fix_time[32];
 char nmea_data[512];
} gps_fix_info_t;

/**
 * @brief Modem initialization interface
 */
typedef struct {
 // Basic modem operations
 bool (*test_modem_ready)(void);
 modem_status_t (*get_modem_status)(void);
 bool (*wait_for_network)(int timeout_seconds);
 
 // Network connectivity
 bool (*test_connectivity)(const char* host, network_test_result_t* result);
 bool (*ping_google)(network_test_result_t* result);
 
 // GPS operations
 bool (*initialize_gps)(void);
 bool (*start_gps_polling)(void);
 bool (*get_gps_fix)(gps_fix_info_t* fix_info);
 bool (*wait_for_gps_fix)(int timeout_seconds, gps_fix_info_t* fix_info);
 
 // Utility functions
 void (*print_status)(void);
 void (*reset_modem)(void);
 
} modem_init_interface_t;

/**
 * @brief Initialize modem initialization module
 * @return Pointer to modem interface
 */
modem_init_interface_t* modem_init_create(void);

/**
 * @brief Cleanup modem initialization module
 */
void modem_init_destroy(void);

/**
 * @brief Complete modem initialization sequence
 * Follows Waveshare SIM7670G recommended startup procedure:
 * 1. Test modem readiness
 * 2. Check SIM card status 
 * 3. Wait for network registration
 * 4. Test connectivity (ping google.com)
 * 5. Initialize GPS
 * 6. Poll GPS until fix acquired
 * 
 * @param timeout_seconds Maximum time to wait for each step
 * @return true if complete sequence successful, false otherwise
 */
bool modem_init_complete_sequence(int timeout_seconds);

#ifdef __cplusplus
}
#endif

#endif // MODEM_INIT_H