#include "gps_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"

static const char *TAG = "GPS_MODULE";

// Module state
static gps_config_t current_config = {0};
static gps_status_t module_status = {0};
static bool module_initialized = false;

// Private function prototypes
static bool gps_init_impl(const gps_config_t* config);
static bool gps_deinit_impl(void);
static bool gps_read_data_impl(gps_data_t* data);
static bool gps_get_status_impl(gps_status_t* status);
static bool gps_power_on_impl(void);
static bool gps_power_off_impl(void);
static bool gps_reset_impl(void);
static void gps_set_debug_impl(bool enable);

// NMEA parsing functions
static bool parse_nmea_coordinate(const char* coord_str, char dir, float* result);
static bool parse_gnrmc(const char* sentence, gps_data_t* data);
static bool parse_gngga(const char* sentence, gps_data_t* data);
static bool parse_gpgsv(const char* sentence, gps_data_t* data);
static bool validate_nmea_checksum(const char* sentence);

// GPS interface implementation
static const gps_interface_t gps_interface = {
    .init = gps_init_impl,
    .deinit = gps_deinit_impl,
    .read_data = gps_read_data_impl,
    .get_status = gps_get_status_impl,
    .power_on = gps_power_on_impl,
    .power_off = gps_power_off_impl,
    .reset = gps_reset_impl,
    .set_debug = gps_set_debug_impl
};

const gps_interface_t* gps_get_interface(void)
{
    return &gps_interface;
}

static bool gps_init_impl(const gps_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    if (module_initialized) {
        ESP_LOGW(TAG, "GPS module already initialized");
        return true;
    }
    
    // Store configuration
    memcpy(&current_config, config, sizeof(gps_config_t));
    
    // Initialize UART (configuration comes from system config)
    const uart_config_t uart_config = {
        .baud_rate = 115200,  // Fixed for SIM7670G
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = uart_param_config(UART_NUM_1, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_NUM_1);
        return false;
    }
    
    ret = uart_set_pin(UART_NUM_1, 18, 17, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_NUM_1);
        return false;
    }
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.initialized = true;
    module_status.uart_ready = true;
    
    module_initialized = true;
    
    if (config->debug_output) {
        ESP_LOGI(TAG, "GPS module initialized successfully");
        ESP_LOGI(TAG, "  Fix timeout: %d ms", config->fix_timeout_ms);
        ESP_LOGI(TAG, "  Min satellites: %d", config->min_satellites);
        ESP_LOGI(TAG, "  Update interval: %d ms", config->data_update_interval_ms);
    }
    
    return true;
}

static bool gps_deinit_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    uart_driver_delete(UART_NUM_1);
    memset(&module_status, 0, sizeof(module_status));
    module_initialized = false;
    
    ESP_LOGI(TAG, "GPS module deinitialized");
    return true;
}

static bool gps_read_data_impl(gps_data_t* data)
{
    if (!module_initialized || !data) {
        return false;
    }
    
    char* buffer = malloc(1024);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return false;
    }
    
    // Clear data structure
    memset(data, 0, sizeof(gps_data_t));
    
    int len = uart_read_bytes(UART_NUM_1, buffer, 1023, pdMS_TO_TICKS(1000));
    if (len > 0) {
        buffer[len] = '\0';
        module_status.total_sentences_parsed++;
        
        // Parse NMEA sentences
        char* line = strtok(buffer, "\r\n");
        while (line != NULL) {
            if (current_config.debug_nmea) {
                ESP_LOGI(TAG, "NMEA: %s", line);
            }
            
            if (validate_nmea_checksum(line)) {
                if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0) {
                    if (parse_gnrmc(line, data)) {
                        module_status.valid_sentences++;
                    }
                } else if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
                    if (parse_gngga(line, data)) {
                        module_status.valid_sentences++;
                    }
                } else if (strncmp(line, "$GPGSV", 6) == 0 || strncmp(line, "$GNGSV", 6) == 0) {
                    parse_gpgsv(line, data);
                }
            } else {
                module_status.parse_errors++;
            }
            
            line = strtok(NULL, "\r\n");
        }
    }
    
    free(buffer);
    
    if (data->fix_valid) {
        module_status.last_fix_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (current_config.debug_output) {
            ESP_LOGI(TAG, "GPS Fix: %.6f, %.6f, %d sats, %.1f km/h, HDOP=%.1f", 
                     data->latitude, data->longitude, data->satellites, 
                     data->speed_kmh, data->hdop);
        }
    } else if (current_config.debug_output) {
        ESP_LOGD(TAG, "GPS: No valid fix (sats: %d)", data->satellites);
    }
    
    return data->fix_valid;
}

static bool gps_get_status_impl(gps_status_t* status)
{
    if (!status) {
        return false;
    }
    
    memcpy(status, &module_status, sizeof(gps_status_t));
    return true;
}

static bool gps_power_on_impl(void)
{
    // This would send AT commands to turn on GPS if needed
    // For SIM7670G, this is handled by the LTE module
    ESP_LOGI(TAG, "GPS power on requested");
    module_status.gps_power_on = true;
    return true;
}

static bool gps_power_off_impl(void)
{
    ESP_LOGI(TAG, "GPS power off requested");
    module_status.gps_power_on = false;
    return true;
}

static bool gps_reset_impl(void)
{
    ESP_LOGI(TAG, "GPS reset requested");
    memset(&module_status, 0, sizeof(module_status));
    module_status.initialized = module_initialized;
    module_status.uart_ready = module_initialized;
    return true;
}

static void gps_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// NMEA parsing implementation
static bool validate_nmea_checksum(const char* sentence)
{
    if (!sentence || strlen(sentence) < 4) {
        return false;
    }
    
    // Find checksum
    const char* checksum_pos = strrchr(sentence, '*');
    if (!checksum_pos) {
        return false; // No checksum
    }
    
    // Calculate checksum
    uint8_t calc_checksum = 0;
    for (const char* p = sentence + 1; p < checksum_pos; p++) {
        calc_checksum ^= *p;
    }
    
    // Parse provided checksum
    uint8_t provided_checksum = (uint8_t)strtol(checksum_pos + 1, NULL, 16);
    
    return calc_checksum == provided_checksum;
}

static bool parse_nmea_coordinate(const char* coord_str, char dir, float* result)
{
    if (!coord_str || strlen(coord_str) < 7 || !result) {
        return false;
    }
    
    float coord = atof(coord_str);
    int degrees = (int)(coord / 100);
    float minutes = coord - (degrees * 100);
    
    *result = degrees + (minutes / 60.0f);
    
    if (dir == 'S' || dir == 'W') {
        *result = -*result;
    }
    
    return true;
}

static bool parse_gnrmc(const char* sentence, gps_data_t* data)
{
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[sizeof(temp_sentence) - 1] = '\0';
    
    char* tokens[12];
    char* token = strtok(temp_sentence, ",");
    int token_count = 0;
    
    while (token && token_count < 12) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    if (token_count < 10) {
        return false;
    }
    
    // Check if fix is valid (A = valid, V = invalid)
    if (tokens[2][0] != 'A') {
        data->fix_valid = false;
        return false;
    }
    
    // Parse latitude and longitude
    if (!parse_nmea_coordinate(tokens[3], tokens[4][0], &data->latitude) ||
        !parse_nmea_coordinate(tokens[5], tokens[6][0], &data->longitude)) {
        return false;
    }
    
    // Parse speed (knots to km/h)
    if (strlen(tokens[7]) > 0) {
        data->speed_kmh = atof(tokens[7]) * 1.852f;
    }
    
    // Parse course
    if (strlen(tokens[8]) > 0) {
        data->course = atof(tokens[8]);
    }
    
    // Create timestamp
    if (strlen(tokens[1]) >= 6 && strlen(tokens[9]) >= 6) {
        snprintf(data->timestamp, sizeof(data->timestamp), 
                "20%c%c-%c%c-%c%cT%c%c:%c%c:%c%c",
                tokens[9][4], tokens[9][5],  // year
                tokens[9][2], tokens[9][3],  // month
                tokens[9][0], tokens[9][1],  // day
                tokens[1][0], tokens[1][1],  // hour
                tokens[1][2], tokens[1][3],  // minute
                tokens[1][4], tokens[1][5]); // second
    }
    
    data->fix_valid = true;
    return true;
}

static bool parse_gngga(const char* sentence, gps_data_t* data)
{
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[sizeof(temp_sentence) - 1] = '\0';
    
    char* tokens[15];
    char* token = strtok(temp_sentence, ",");
    int token_count = 0;
    
    while (token && token_count < 15) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    if (token_count < 11) {
        return false;
    }
    
    // Parse fix quality
    if (strlen(tokens[6]) > 0) {
        data->fix_quality = tokens[6][0];
    }
    
    // Parse altitude
    if (strlen(tokens[9]) > 0) {
        data->altitude = atof(tokens[9]);
    }
    
    // Parse HDOP
    if (strlen(tokens[8]) > 0) {
        data->hdop = atof(tokens[8]);
    }
    
    return true;
}

static bool parse_gpgsv(const char* sentence, gps_data_t* data)
{
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    
    char* token = strtok(temp_sentence, ",");
    int field = 0;
    
    while (token && field < 4) {
        if (field == 3) { // Satellites in view
            data->satellites = atoi(token);
            return true;
        }
        token = strtok(NULL, ",");
        field++;
    }
    
    return false;
}

// Utility functions
bool gps_is_fix_valid(const gps_data_t* data)
{
    return data && data->fix_valid && 
           data->satellites >= current_config.min_satellites &&
           data->hdop > 0 && data->hdop < 20.0f; // Reasonable HDOP range
}

float gps_calculate_distance(float lat1, float lon1, float lat2, float lon2)
{
    const float R = 6371000; // Earth radius in meters
    float dlat = (lat2 - lat1) * M_PI / 180.0f;
    float dlon = (lon2 - lon1) * M_PI / 180.0f;
    float a = sin(dlat/2) * sin(dlat/2) + cos(lat1 * M_PI / 180.0f) * cos(lat2 * M_PI / 180.0f) * sin(dlon/2) * sin(dlon/2);
    float c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

bool gps_format_coordinates(const gps_data_t* data, char* buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size < 32) {
        return false;
    }
    
    snprintf(buffer, buffer_size, "%.6f,%.6f", data->latitude, data->longitude);
    return true;
}