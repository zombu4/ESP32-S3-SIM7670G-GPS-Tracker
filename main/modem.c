#include "tracker.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "MODEM";

// Use same UART as GPS since they share the same module
extern bool gps_init(void); // We'll reuse the UART initialization

static bool modem_initialized = false;

static bool send_at_command(const char* command, const char* expected_response, int timeout_ms)
{
    if (!command) return false;
    
    // Send command
    char cmd_buffer[256];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s\r\n", command);
    uart_write_bytes(UART_NUM_1, cmd_buffer, strlen(cmd_buffer));
    
    ESP_LOGI(TAG, "AT CMD: %s", command);
    
    if (!expected_response) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return true;
    }
    
    // Wait for response
    char* response_buffer = malloc(1024);
    if (!response_buffer) return false;
    
    int total_len = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        int len = uart_read_bytes(UART_NUM_1, response_buffer + total_len, 
                                 1023 - total_len, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
            response_buffer[total_len] = '\0';
            
            if (strstr(response_buffer, expected_response)) {
                ESP_LOGI(TAG, "AT RSP: Found '%s'", expected_response);
                free(response_buffer);
                return true;
            }
            
            // Also log any error responses
            if (strstr(response_buffer, "ERROR") || strstr(response_buffer, "+CME ERROR")) {
                ESP_LOGW(TAG, "AT ERR: %s", response_buffer);
                break;
            }
        }
    }
    
    ESP_LOGW(TAG, "AT TIMEOUT: Expected '%s', got '%s'", expected_response, response_buffer);
    free(response_buffer);
    return false;
}

bool modem_init(void)
{
    if (!gps_init()) {
        ESP_LOGE(TAG, "Failed to initialize UART for modem");
        return false;
    }
    
    ESP_LOGI(TAG, "Initializing SIM7670G modem...");
    
    // Wait for module to be ready
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test AT communication
    for (int i = 0; i < 5; i++) {
        if (send_at_command("AT", "OK", 2000)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (i == 4) {
            ESP_LOGE(TAG, "Failed to establish AT communication");
            return false;
        }
    }
    
    // Turn on full functionality
    send_at_command("AT+CFUN=1", "OK", 10000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Check SIM card status
    if (!send_at_command("AT+CPIN?", "READY", 5000)) {
        ESP_LOGE(TAG, "SIM card not ready");
        return false;
    }
    
    // Enable GPS
    send_at_command("AT+CGNSSPWR=1", "OK", 5000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Start GPS data output
    send_at_command("AT+CGNSSTST=1", "OK", 5000);
    
    modem_initialized = true;
    ESP_LOGI(TAG, "Modem initialized successfully");
    return true;
}

bool modem_connect_network(void)
{
    if (!modem_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "Connecting to cellular network...");
    
    // Check network registration
    for (int i = 0; i < 30; i++) { // Wait up to 30 seconds
        if (send_at_command("AT+CREG?", "+CREG: 0,1", 2000) ||
            send_at_command("AT+CREG?", "+CREG: 0,5", 2000)) {
            ESP_LOGI(TAG, "Network registered");
            break;
        }
        
        if (i == 29) {
            ESP_LOGE(TAG, "Failed to register to network");
            return false;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Check signal quality
    send_at_command("AT+CSQ", NULL, 2000);
    
    // Configure PDP context with your specific APN
    send_at_command("AT+CGDCONT=1,\"IP\",\"m2mglobal\"", "OK", 5000);
    
    // Activate PDP context
    if (!send_at_command("AT+CGACT=1,1", "OK", 30000)) {
        ESP_LOGW(TAG, "Failed to activate PDP context, trying alternative");
        // Some carriers need different activation
        send_at_command("AT+CGATT=1", "OK", 10000);
    }
    
    ESP_LOGI(TAG, "Connected to cellular network");
    return true;
}