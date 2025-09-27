/**
 * 🛰️ GPS NMEA PARSER MODULE - Parse NMEA sentences and extract GPS fix status
 * 
 * Separate module for GPS NMEA parsing following modular architecture
 * Easy to debug, test, and maintain independently
 */

#ifndef GPS_NMEA_PARSER_H
#define GPS_NMEA_PARSER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // Fix status
    bool has_valid_fix;
    uint8_t satellites_used;
    uint8_t fix_quality;
    
    // Location data
    double latitude;
    double longitude;
    float altitude;
    float hdop;  // Horizontal dilution of precision
    
    // Time data
    uint32_t timestamp;
    
    // Raw NMEA info for debugging
    char last_gga_sentence[128];
    uint32_t sentences_parsed;
    uint32_t valid_fixes_count;
} gps_nmea_data_t;

typedef struct {
    bool (*parse_nmea_sentence)(const char* sentence, gps_nmea_data_t* data);
    bool (*has_valid_fix)(const gps_nmea_data_t* data);
    void (*get_location)(const gps_nmea_data_t* data, double* lat, double* lon, float* alt);
    void (*get_fix_info)(const gps_nmea_data_t* data, uint8_t* satellites, uint8_t* quality, float* hdop);
    void (*reset_data)(gps_nmea_data_t* data);
    void (*get_debug_info)(const gps_nmea_data_t* data, char* debug_str, size_t max_len);
} gps_nmea_parser_interface_t;

// Get the NMEA parser interface
const gps_nmea_parser_interface_t* gps_nmea_parser_get_interface(void);

#endif // GPS_NMEA_PARSER_H