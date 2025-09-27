/**
 * 🛰️ GPS NMEA PARSER MODULE IMPLEMENTATION
 * 
 * Parses NMEA sentences to extract GPS fix status and coordinates
 * Separate module for easy debugging and testing
 */

#include "gps_nmea_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "GPS_NMEA_PARSER";

// 🛰️ NMEA PARSING FUNCTIONS 🛰️

static bool parse_gga_sentence(const char* sentence, gps_nmea_data_t* data);
static double nmea_to_decimal_degrees(const char* coord_str, const char* direction);
static bool validate_nmea_checksum(const char* sentence);

// 🛰️ MAIN NMEA SENTENCE PARSER 🛰️

static bool gps_parse_nmea_sentence_impl(const char* sentence, gps_nmea_data_t* data)
{
    if (!sentence || !data) {
        return false;
    }
    
    // Validate NMEA sentence format
    if (sentence[0] != '$' || strlen(sentence) < 10) {
        return false;
    }
    
    // Validate checksum
    if (!validate_nmea_checksum(sentence)) {
        ESP_LOGW(TAG, "Invalid NMEA checksum: %.32s", sentence);
        return false;
    }
    
    data->sentences_parsed++;
    
    // Parse different NMEA sentence types
    if (strncmp(sentence, "$GNGGA", 6) == 0 || strncmp(sentence, "$GPGGA", 6) == 0) {
        ESP_LOGD(TAG, "🛰️ Parsing GGA sentence: %.64s", sentence);
        return parse_gga_sentence(sentence, data);
    }
    
    // Add support for other sentence types if needed (RMC, GSA, etc.)
    ESP_LOGV(TAG, "Unhandled NMEA sentence type: %.16s", sentence);
    return false;
}

// 🛰️ GGA SENTENCE PARSER (GPS Fix Data) 🛰️

static bool parse_gga_sentence(const char* sentence, gps_nmea_data_t* data)
{
    // GGA sentence format:
    // $GNGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,geoidHeight,M,dgpsAge,dgpsID*checksum
    
    char sentence_copy[256];
    strncpy(sentence_copy, sentence, sizeof(sentence_copy) - 1);
    sentence_copy[sizeof(sentence_copy) - 1] = '\0';
    
    // Store the sentence for debugging
    strncpy(data->last_gga_sentence, sentence, sizeof(data->last_gga_sentence) - 1);
    data->last_gga_sentence[sizeof(data->last_gga_sentence) - 1] = '\0';
    
    char* token = strtok(sentence_copy, ",");
    int field = 0;
    
    char time_str[16] = {0};
    char lat_str[16] = {0};
    char lat_dir = 'N';
    char lon_str[16] = {0};
    char lon_dir = 'E';
    char quality_str[4] = {0};
    char satellites_str[4] = {0};
    char hdop_str[8] = {0};
    char alt_str[16] = {0};
    
    while (token != NULL && field < 12) {
        switch (field) {
            case 1: strncpy(time_str, token, sizeof(time_str) - 1); break;
            case 2: strncpy(lat_str, token, sizeof(lat_str) - 1); break;
            case 3: if (strlen(token) > 0) lat_dir = token[0]; break;
            case 4: strncpy(lon_str, token, sizeof(lon_str) - 1); break;
            case 5: if (strlen(token) > 0) lon_dir = token[0]; break;
            case 6: strncpy(quality_str, token, sizeof(quality_str) - 1); break;
            case 7: strncpy(satellites_str, token, sizeof(satellites_str) - 1); break;
            case 8: strncpy(hdop_str, token, sizeof(hdop_str) - 1); break;
            case 9: strncpy(alt_str, token, sizeof(alt_str) - 1); break;
        }
        token = strtok(NULL, ",");
        field++;
    }
    
    // Parse fix quality and satellite count
    int quality = (strlen(quality_str) > 0) ? atoi(quality_str) : 0;
    int satellites = (strlen(satellites_str) > 0) ? atoi(satellites_str) : 0;
    
    ESP_LOGD(TAG, "🛰️ GGA parsed: quality=%d, satellites=%d, lat=%s%c, lon=%s%c", 
             quality, satellites, lat_str, lat_dir, lon_str, lon_dir);
    
    // Update fix status (quality > 0 means we have a fix)
    bool has_fix = (quality > 0 && satellites > 0 && strlen(lat_str) > 0 && strlen(lon_str) > 0);
    
    if (has_fix) {
        data->has_valid_fix = true;
        data->fix_quality = quality;
        data->satellites_used = satellites;
        
        // Convert coordinates to decimal degrees
        data->latitude = nmea_to_decimal_degrees(lat_str, &lat_dir);
        data->longitude = nmea_to_decimal_degrees(lon_str, &lon_dir);
        
        if (strlen(alt_str) > 0) {
            data->altitude = atof(alt_str);
        }
        
        if (strlen(hdop_str) > 0) {
            data->hdop = atof(hdop_str);
        }
        
        data->valid_fixes_count++;
        data->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        ESP_LOGI(TAG, "✅ GPS FIX: %.6f°N, %.6f°E, %d satellites, quality=%d", 
                 data->latitude, data->longitude, satellites, quality);
        
        return true;
    } else {
        // Clear fix status but don't reset counters
        data->has_valid_fix = false;
        ESP_LOGD(TAG, "⚠️ No GPS fix: quality=%d, satellites=%d", quality, satellites);
        return false;
    }
}

// 🛰️ COORDINATE CONVERSION 🛰️

static double nmea_to_decimal_degrees(const char* coord_str, const char* direction)
{
    if (!coord_str || strlen(coord_str) == 0) {
        return 0.0;
    }
    
    // NMEA format: DDMM.MMMMM or DDDMM.MMMMM
    double coord = atof(coord_str);
    
    // Extract degrees and minutes
    int degrees = (int)(coord / 100.0);
    double minutes = coord - (degrees * 100.0);
    
    // Convert to decimal degrees
    double decimal = degrees + (minutes / 60.0);
    
    // Apply direction (South and West are negative)
    if (direction && (*direction == 'S' || *direction == 'W')) {
        decimal = -decimal;
    }
    
    return decimal;
}

// 🛰️ NMEA CHECKSUM VALIDATION 🛰️

static bool validate_nmea_checksum(const char* sentence)
{
    if (!sentence || strlen(sentence) < 4) {
        return false;
    }
    
    // Find the checksum delimiter (*)
    const char* checksum_pos = strrchr(sentence, '*');
    if (!checksum_pos || strlen(checksum_pos) < 3) {
        return false; // No checksum or too short
    }
    
    // Calculate checksum of sentence data (between $ and *)
    uint8_t calculated_checksum = 0;
    for (const char* p = sentence + 1; p < checksum_pos; p++) {
        calculated_checksum ^= *p;
    }
    
    // Parse provided checksum
    char checksum_str[3] = {checksum_pos[1], checksum_pos[2], '\0'};
    uint8_t provided_checksum = (uint8_t)strtol(checksum_str, NULL, 16);
    
    return (calculated_checksum == provided_checksum);
}

// 🛰️ INTERFACE FUNCTIONS 🛰️

static bool gps_has_valid_fix_impl(const gps_nmea_data_t* data)
{
    return data && data->has_valid_fix && data->satellites_used > 0;
}

static void gps_get_location_impl(const gps_nmea_data_t* data, double* lat, double* lon, float* alt)
{
    if (!data) return;
    
    if (lat) *lat = data->latitude;
    if (lon) *lon = data->longitude; 
    if (alt) *alt = data->altitude;
}

static void gps_get_fix_info_impl(const gps_nmea_data_t* data, uint8_t* satellites, uint8_t* quality, float* hdop)
{
    if (!data) return;
    
    if (satellites) *satellites = data->satellites_used;
    if (quality) *quality = data->fix_quality;
    if (hdop) *hdop = data->hdop;
}

static void gps_reset_data_impl(gps_nmea_data_t* data)
{
    if (!data) return;
    
    memset(data, 0, sizeof(gps_nmea_data_t));
}

static void gps_get_debug_info_impl(const gps_nmea_data_t* data, char* debug_str, size_t max_len)
{
    if (!data || !debug_str) return;
    
    snprintf(debug_str, max_len, 
        "GPS: fix=%s, sat=%d, quality=%d, lat=%.6f, lon=%.6f, alt=%.1f, hdop=%.2f, parsed=%u, fixes=%u",
        data->has_valid_fix ? "YES" : "NO",
        data->satellites_used,
        data->fix_quality,
        data->latitude,
        data->longitude,
        data->altitude,
        data->hdop,
        data->sentences_parsed,
        data->valid_fixes_count);
}

// 🛰️ INTERFACE IMPLEMENTATION 🛰️

static const gps_nmea_parser_interface_t gps_nmea_parser_interface = {
    .parse_nmea_sentence = gps_parse_nmea_sentence_impl,
    .has_valid_fix = gps_has_valid_fix_impl,
    .get_location = gps_get_location_impl,
    .get_fix_info = gps_get_fix_info_impl,
    .reset_data = gps_reset_data_impl,
    .get_debug_info = gps_get_debug_info_impl,
};

const gps_nmea_parser_interface_t* gps_nmea_parser_get_interface(void)
{
    return &gps_nmea_parser_interface;
}