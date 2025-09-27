// Baud Rate Testing Implementation for ESP32-S3-SIM7670G
// Systematically tests UART communication at different baud rates

#include "baud_rate_tester.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "BAUD_TESTER";

bool configure_uart_with_baud(int tx_pin, int rx_pin, int baud_rate)
{
 // First, delete the UART driver if it exists
 uart_driver_delete(UART_NUM_1);
 
 ESP_LOGI(TAG, "Testing UART configuration: TX=%d, RX=%d, Baud=%d", 
 tx_pin, rx_pin, baud_rate);
 
 uart_config_t uart_config = {
 .baud_rate = baud_rate,
 .data_bits = UART_DATA_8_BITS,
 .parity = UART_PARITY_DISABLE,
 .stop_bits = UART_STOP_BITS_1,
 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
 .rx_flow_ctrl_thresh = 122,
 };
 
 // Configure UART parameters
 esp_err_t err = uart_param_config(UART_NUM_1, &uart_config);
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
 return false;
 }
 
 // Set UART pins
 err = uart_set_pin(UART_NUM_1, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
 return false;
 }
 
 // Install UART driver
 err = uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
 if (err != ESP_OK) {
 ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
 return false;
 }
 
 // Flush any existing data
 uart_flush(UART_NUM_1);
 vTaskDelay(pdMS_TO_TICKS(100));
 
 ESP_LOGI(TAG, "UART configured successfully at %d baud", baud_rate);
 return true;
}

bool test_at_command_simple(void)
{
 const char* at_cmd = "AT\r\n";
 char response_buffer[128] = {0};
 
 // Clear any pending data
 uart_flush_input(UART_NUM_1);
 
 // Send AT command
 int written = uart_write_bytes(UART_NUM_1, at_cmd, strlen(at_cmd));
 if (written < 0) {
 ESP_LOGW(TAG, "UART write failed: %d", written);
 return false;
 }
 
 ESP_LOGI(TAG, "Sent AT command, written %d bytes", written);
 
 // Wait for response
 vTaskDelay(pdMS_TO_TICKS(500));
 
 // Read response
 int len = uart_read_bytes(UART_NUM_1, (uint8_t*)response_buffer, 
 sizeof(response_buffer) - 1, pdMS_TO_TICKS(1000));
 
 if (len > 0) {
 response_buffer[len] = '\0';
 ESP_LOGI(TAG, " RESPONSE (%d bytes): '%s'", len, response_buffer);
 
 // Check for OK response
 if (strstr(response_buffer, "OK") != NULL) {
 ESP_LOGI(TAG, " SUCCESS: Found 'OK' response!");
 return true;
 } else {
 ESP_LOGI(TAG, " Got response but no 'OK' found");
 return false;
 }
 } else if (len == 0) {
 ESP_LOGW(TAG, " No response received (timeout)");
 return false;
 } else {
 ESP_LOGE(TAG, " UART read error: %d", len);
 return false;
 }
}

bool test_uart_baud_rates(int tx_pin, int rx_pin, baud_test_result_t* result)
{
 if (!result) {
 ESP_LOGE(TAG, "Result pointer is NULL");
 return false;
 }
 
 // Initialize result
 memset(result, 0, sizeof(baud_test_result_t));
 result->found_working_rate = false;
 result->working_baud_rate = -1;
 
 ESP_LOGI(TAG, " Starting systematic baud rate test on TX=%d, RX=%d", tx_pin, rx_pin);
 ESP_LOGI(TAG, " Testing %d different baud rates...", NUM_BAUD_RATES);
 
 for (int i = 0; i < NUM_BAUD_RATES; i++) {
 int baud_rate = test_baud_rates[i];
 
 ESP_LOGI(TAG, "\n Test %d/%d: Trying baud rate %d", 
 i + 1, NUM_BAUD_RATES, baud_rate);
 
 // Configure UART with this baud rate
 if (!configure_uart_with_baud(tx_pin, rx_pin, baud_rate)) {
 ESP_LOGW(TAG, " Failed to configure UART at %d baud", baud_rate);
 continue;
 }
 
 // Test AT command
 bool success = test_at_command_simple();
 
 if (success) {
 ESP_LOGI(TAG, " FOUND WORKING BAUD RATE: %d", baud_rate);
 result->found_working_rate = true;
 result->working_baud_rate = baud_rate;
 return true;
 }
 
 ESP_LOGI(TAG, " Baud rate %d failed", baud_rate);
 vTaskDelay(pdMS_TO_TICKS(200)); // Brief pause between tests
 }
 
 ESP_LOGE(TAG, "💥 NO WORKING BAUD RATE FOUND!");
 ESP_LOGE(TAG, " Check hardware connections:");
 ESP_LOGE(TAG, " - SIM7670G module power");
 ESP_LOGE(TAG, " - DIP switch settings");
 ESP_LOGE(TAG, " - TX/RX pin connections");
 ESP_LOGE(TAG, " - Module initialization sequence");
 
 return false;
}