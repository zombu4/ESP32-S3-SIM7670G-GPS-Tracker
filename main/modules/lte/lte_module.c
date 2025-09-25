#include "lte_module.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "stdlib.h"

static const char *TAG = "LTE_MODULE";

// Module state
static lte_config_t current_config = {0};
static lte_module_status_t module_status = {0};
static bool module_initialized = false;

// Private function prototypes
static bool lte_init_impl(const lte_config_t* config);
static bool lte_deinit_impl(void);
static bool lte_connect_impl(void);
static bool lte_disconnect_impl(void);
static lte_status_t lte_get_connection_status_impl(void);
static bool lte_get_status_impl(lte_module_status_t* status);
static bool lte_get_network_info_impl(lte_network_info_t* info);
static bool lte_send_at_command_impl(const char* command, at_response_t* response, int timeout_ms);
static bool lte_set_apn_impl(const char* apn, const char* username, const char* password);
static bool lte_check_sim_ready_impl(void);
static bool lte_get_signal_strength_impl(int* rssi, int* quality);
static void lte_set_debug_impl(bool enable);

// Helper functions
static bool wait_for_at_response(const char* expected, at_response_t* response, int timeout_ms);
static bool parse_signal_quality(const char* response, int* rssi, int* quality);
static bool parse_network_info(const char* response, lte_network_info_t* info);

// LTE interface implementation
static const lte_interface_t lte_interface = {
    .init = lte_init_impl,
    .deinit = lte_deinit_impl,
    .connect = lte_connect_impl,
    .disconnect = lte_disconnect_impl,
    .get_connection_status = lte_get_connection_status_impl,
    .get_status = lte_get_status_impl,
    .get_network_info = lte_get_network_info_impl,
    .send_at_command = lte_send_at_command_impl,
    .set_apn = lte_set_apn_impl,
    .check_sim_ready = lte_check_sim_ready_impl,
    .get_signal_strength = lte_get_signal_strength_impl,
    .set_debug = lte_set_debug_impl
};

const lte_interface_t* lte_get_interface(void)
{
    return &lte_interface;
}

static bool lte_init_impl(const lte_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    if (module_initialized) {
        ESP_LOGW(TAG, "LTE module already initialized");
        return true;
    }
    
    // Store configuration
    memcpy(&current_config, config, sizeof(lte_config_t));
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.connection_status = LTE_STATUS_DISCONNECTED;
    
    // Wait for module to be ready
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test AT communication
    at_response_t response;
    for (int i = 0; i < current_config.max_retries; i++) {
        if (lte_send_at_command_impl("AT", &response, 2000)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (i == current_config.max_retries - 1) {
            ESP_LOGE(TAG, "Failed to establish AT communication");
            return false;
        }
    }
    
    // Set full functionality
    if (!lte_send_at_command_impl("AT+CFUN=1", &response, 10000)) {
        ESP_LOGE(TAG, "Failed to set full functionality");
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Check SIM status
    if (!lte_check_sim_ready_impl()) {
        ESP_LOGE(TAG, "SIM card not ready");
        return false;
    }
    
    module_status.initialized = true;
    module_initialized = true;
    
    if (config->debug_output) {
        ESP_LOGI(TAG, "LTE module initialized successfully");
        ESP_LOGI(TAG, "  APN: '%s'", config->apn);
        ESP_LOGI(TAG, "  Network timeout: %d ms", config->network_timeout_ms);
        ESP_LOGI(TAG, "  Max retries: %d", config->max_retries);
    }
    
    return true;
}

static bool lte_deinit_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    // Disconnect if connected
    lte_disconnect_impl();
    
    // Reset module status
    memset(&module_status, 0, sizeof(module_status));
    module_initialized = false;
    
    ESP_LOGI(TAG, "LTE module deinitialized");
    return true;
}

static bool lte_connect_impl(void)
{
    if (!module_initialized) {
        ESP_LOGE(TAG, "LTE module not initialized");
        return false;
    }
    
    if (module_status.connection_status == LTE_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "Already connected to network");
        return true;
    }
    
    ESP_LOGI(TAG, "Connecting to cellular network...");
    module_status.connection_status = LTE_STATUS_CONNECTING;
    
    at_response_t response;
    
    // Set APN
    if (!lte_set_apn_impl(current_config.apn, current_config.username, current_config.password)) {
        module_status.connection_status = LTE_STATUS_ERROR;
        return false;
    }
    
    // Check network registration
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < current_config.network_timeout_ms) {
        if (lte_send_at_command_impl("AT+CREG?", &response, 2000)) {
            if (strstr(response.response, "+CREG: 0,1") || strstr(response.response, "+CREG: 0,5")) {
                ESP_LOGI(TAG, "Network registered successfully");
                break;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // Activate PDP context
    if (!lte_send_at_command_impl("AT+CGACT=1,1", &response, 30000)) {
        ESP_LOGW(TAG, "Failed to activate PDP context, trying alternative");
        if (!lte_send_at_command_impl("AT+CGATT=1", &response, 10000)) {
            ESP_LOGE(TAG, "Failed to attach to network");
            module_status.connection_status = LTE_STATUS_ERROR;
            return false;
        }
    }
    
    module_status.pdp_active = true;
    module_status.connection_status = LTE_STATUS_CONNECTED;
    module_status.connection_uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Get network info
    lte_get_network_info_impl(&module_status.network_info);
    
    ESP_LOGI(TAG, "Connected to cellular network");
    return true;
}

static bool lte_disconnect_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    at_response_t response;
    
    // Deactivate PDP context
    lte_send_at_command_impl("AT+CGACT=0,1", &response, 10000);
    
    module_status.connection_status = LTE_STATUS_DISCONNECTED;
    module_status.pdp_active = false;
    
    ESP_LOGI(TAG, "Disconnected from cellular network");
    return true;
}

static lte_status_t lte_get_connection_status_impl(void)
{
    return module_status.connection_status;
}

static bool lte_get_status_impl(lte_module_status_t* status)
{
    if (!status) {
        return false;
    }
    
    memcpy(status, &module_status, sizeof(lte_module_status_t));
    return true;
}

static bool lte_get_network_info_impl(lte_network_info_t* info)
{
    if (!info || !module_initialized) {
        return false;
    }
    
    at_response_t response;
    
    // Get operator name
    if (lte_send_at_command_impl("AT+COPS?", &response, 5000)) {
        parse_network_info(response.response, info);
    }
    
    // Get signal strength
    int rssi, quality;
    if (lte_get_signal_strength_impl(&rssi, &quality)) {
        info->signal_strength = rssi;
        info->signal_quality = quality;
    }
    
    return true;
}

static bool lte_send_at_command_impl(const char* command, at_response_t* response, int timeout_ms)
{
    if (!command || !response) {
        return false;
    }
    
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Clear response
    memset(response, 0, sizeof(at_response_t));
    
    // Send command
    char cmd_buffer[256];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s\r\n", command);
    uart_write_bytes(UART_NUM_1, cmd_buffer, strlen(cmd_buffer));
    
    if (current_config.debug_at_commands) {
        ESP_LOGI(TAG, "AT CMD: %s", command);
    }
    
    // Wait for response
    response->success = wait_for_at_response("OK", response, timeout_ms);
    response->response_time_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_time;
    
    if (current_config.debug_at_commands) {
        ESP_LOGI(TAG, "AT RSP: %s (%d ms)", 
                 response->success ? "OK" : "ERROR", response->response_time_ms);
        if (!response->success && strlen(response->response) > 0) {
            ESP_LOGW(TAG, "AT ERR: %s", response->response);
        }
    }
    
    return response->success;
}

static bool lte_set_apn_impl(const char* apn, const char* username, const char* password)
{
    if (!apn) {
        return false;
    }
    
    char apn_cmd[128];
    snprintf(apn_cmd, sizeof(apn_cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    
    at_response_t response;
    bool success = lte_send_at_command_impl(apn_cmd, &response, 5000);
    
    if (success && current_config.debug_output) {
        ESP_LOGI(TAG, "APN set to '%s'", apn);
    }
    
    return success;
}

static bool lte_check_sim_ready_impl(void)
{
    at_response_t response;
    
    if (lte_send_at_command_impl("AT+CPIN?", &response, 5000)) {
        if (strstr(response.response, "READY")) {
            module_status.sim_ready = true;
            ESP_LOGI(TAG, "SIM card is ready");
            return true;
        }
    }
    
    module_status.sim_ready = false;
    ESP_LOGE(TAG, "SIM card not ready");
    return false;
}

static bool lte_get_signal_strength_impl(int* rssi, int* quality)
{
    if (!rssi || !quality) {
        return false;
    }
    
    at_response_t response;
    if (lte_send_at_command_impl("AT+CSQ", &response, 2000)) {
        return parse_signal_quality(response.response, rssi, quality);
    }
    
    return false;
}

static void lte_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    current_config.debug_at_commands = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// Helper function implementations
static bool wait_for_at_response(const char* expected, at_response_t* response, int timeout_ms)
{
    if (!expected || !response) {
        return false;
    }
    
    char* buffer = malloc(512);
    if (!buffer) {
        return false;
    }
    
    int total_len = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(timeout_ms)) {
        int len = uart_read_bytes(UART_NUM_1, buffer + total_len, 
                                 511 - total_len, pdMS_TO_TICKS(100));
        if (len > 0) {
            total_len += len;
            buffer[total_len] = '\0';
            
            // Copy to response buffer
            strncpy(response->response, buffer, sizeof(response->response) - 1);
            response->response[sizeof(response->response) - 1] = '\0';
            
            if (strstr(buffer, expected)) {
                free(buffer);
                return true;
            }
            
            // Check for error responses
            if (strstr(buffer, "ERROR") || strstr(buffer, "+CME ERROR") || strstr(buffer, "+CMS ERROR")) {
                free(buffer);
                return false;
            }
        }
    }
    
    free(buffer);
    return false;
}

static bool parse_signal_quality(const char* response, int* rssi, int* quality)
{
    if (!response || !rssi || !quality) {
        return false;
    }
    
    // Look for +CSQ: response
    const char* csq_pos = strstr(response, "+CSQ:");
    if (!csq_pos) {
        return false;
    }
    
    int parsed_rssi, parsed_quality;
    if (sscanf(csq_pos, "+CSQ: %d,%d", &parsed_rssi, &parsed_quality) == 2) {
        *rssi = (parsed_rssi == 99) ? -113 : (-113 + parsed_rssi * 2); // Convert to dBm
        *quality = parsed_quality;
        return true;
    }
    
    return false;
}

static bool parse_network_info(const char* response, lte_network_info_t* info)
{
    if (!response || !info) {
        return false;
    }
    
    // Parse operator name from +COPS response
    const char* cops_pos = strstr(response, "+COPS:");
    if (cops_pos) {
        // Simple parsing - look for quoted operator name
        const char* quote_start = strchr(cops_pos, '"');
        if (quote_start) {
            quote_start++; // Move past the quote
            const char* quote_end = strchr(quote_start, '"');
            if (quote_end && (quote_end - quote_start) < sizeof(info->operator_name) - 1) {
                strncpy(info->operator_name, quote_start, quote_end - quote_start);
                info->operator_name[quote_end - quote_start] = '\0';
            }
        }
    }
    
    return true;
}

// Utility functions
const char* lte_status_to_string(lte_status_t status)
{
    switch (status) {
        case LTE_STATUS_DISCONNECTED: return "Disconnected";
        case LTE_STATUS_CONNECTING: return "Connecting";
        case LTE_STATUS_CONNECTED: return "Connected";
        case LTE_STATUS_ERROR: return "Error";
        default: return "Unknown";
    }
}

bool lte_is_connected(void)
{
    return module_status.connection_status == LTE_STATUS_CONNECTED;
}

bool lte_format_network_info(const lte_network_info_t* info, char* buffer, size_t buffer_size)
{
    if (!info || !buffer || buffer_size < 64) {
        return false;
    }
    
    snprintf(buffer, buffer_size, "Operator: %s, Signal: %d dBm, Quality: %d",
             info->operator_name, info->signal_strength, info->signal_quality);
    return true;
}