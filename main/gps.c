#include "tracker.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "stdlib.h"

static const char *TAG = "GPS";

#define UART_NUM UART_NUM_1
#define TXD_PIN 18
#define RXD_PIN 17
#define BUF_SIZE 1024

static bool gps_initialized = false;

bool gps_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        return false;
    }
    
    ret = uart_param_config(UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART");
        return false;
    }
    
    ret = uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins");
        return false;
    }
    
    gps_initialized = true;
    ESP_LOGI(TAG, "GPS UART initialized");
    return true;
}

static bool parse_nmea_coordinate(const char* coord_str, char dir, float* result)
{
    if (!coord_str || strlen(coord_str) < 7) {
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
    // $GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
    char* tokens[12];
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    temp_sentence[sizeof(temp_sentence) - 1] = '\0';
    
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
    
    // Parse latitude
    if (!parse_nmea_coordinate(tokens[3], tokens[4][0], &data->latitude)) {
        return false;
    }
    
    // Parse longitude
    if (!parse_nmea_coordinate(tokens[5], tokens[6][0], &data->longitude)) {
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
    
    // Create timestamp from date and time
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

static bool parse_gpgsv(const char* sentence, gps_data_t* data)
{
    // $GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75
    char* tokens[20];
    char temp_sentence[256];
    strncpy(temp_sentence, sentence, sizeof(temp_sentence) - 1);
    
    char* token = strtok(temp_sentence, ",");
    int token_count = 0;
    
    while (token && token_count < 4) {
        tokens[token_count] = token;
        token = strtok(NULL, ",");
        token_count++;
    }
    
    if (token_count >= 4) {
        data->satellites = atoi(tokens[3]);
        return true;
    }
    
    return false;
}

bool gps_read_data(gps_data_t *data)
{
    if (!gps_initialized || !data) {
        return false;
    }
    
    char* buffer = malloc(BUF_SIZE);
    if (!buffer) {
        return false;
    }
    
    // Clear data structure
    memset(data, 0, sizeof(gps_data_t));
    
    int len = uart_read_bytes(UART_NUM, buffer, BUF_SIZE - 1, pdMS_TO_TICKS(1000));
    if (len > 0) {
        buffer[len] = '\0';
        
        // Look for NMEA sentences
        char* line = strtok(buffer, "\r\n");
        while (line != NULL) {
            if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0) {
                parse_gnrmc(line, data);
            } else if (strncmp(line, "$GPGSV", 6) == 0 || strncmp(line, "$GNGSV", 6) == 0) {
                parse_gpgsv(line, data);
            }
            line = strtok(NULL, "\r\n");
        }
    }
    
    free(buffer);
    
    if (data->fix_valid) {
        ESP_LOGI(TAG, "GPS: %.6f, %.6f, %d sats, %.1f km/h", 
                 data->latitude, data->longitude, data->satellites, data->speed_kmh);
    } else {
        ESP_LOGD(TAG, "GPS: No valid fix");
    }
    
    return data->fix_valid;
}