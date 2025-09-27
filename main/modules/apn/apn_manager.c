#include "apn_manager.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "../lte/lte_module.h"

static const char *TAG = "APN_MANAGER";

// Default APN Configuration
const apn_config_t APN_CONFIG_DEFAULT = {
    .apn = "m2mglobal",
    .username = "",
    .password = "",
    .auto_detect = false,
    .persistence = true,
    .debug = false
};

// Module state
static apn_config_t current_config = {0};
static apn_status_t module_status = {0};
static bool module_initialized = false;

// NVS namespace for APN persistence
#define APN_NVS_NAMESPACE "apn_config"
#define APN_NVS_KEY_APN "apn"
#define APN_NVS_KEY_USER "username" 
#define APN_NVS_KEY_PASS "password"
#define APN_NVS_KEY_TIME "config_time"

// Private function prototypes
static bool apn_init_impl(const apn_config_t* config);
static bool apn_deinit_impl(void);
static bool apn_check_configuration_impl(apn_status_t* status);
static bool apn_set_apn_impl(const char* apn, const char* username, const char* password);
static bool apn_activate_context_impl(void);
static bool apn_deactivate_context_impl(void);
static bool apn_get_status_impl(apn_status_t* status);
static bool apn_get_ip_address_impl(char* ip_buffer, size_t buffer_size);
static bool apn_is_ready_for_data_impl(void);
static bool apn_save_to_nvs_impl(void);
static bool apn_load_from_nvs_impl(void);
static void apn_set_debug_impl(bool enable);

// Helper functions
static bool query_modem_apn_config(char* apn_buffer, size_t buffer_size);
static bool query_pdp_context_status(void);
static uint32_t get_timestamp_ms(void);

// APN Manager Interface
static const apn_manager_interface_t apn_interface = {
    .init = apn_init_impl,
    .deinit = apn_deinit_impl,
    .check_configuration = apn_check_configuration_impl,
    .set_apn = apn_set_apn_impl,
    .activate_context = apn_activate_context_impl,
    .deactivate_context = apn_deactivate_context_impl,
    .get_status = apn_get_status_impl,
    .is_ready_for_data = apn_is_ready_for_data_impl,
    .save_to_nvs = apn_save_to_nvs_impl,
    .load_from_nvs = apn_load_from_nvs_impl,
    .set_debug = apn_set_debug_impl
};

const apn_manager_interface_t* apn_manager_get_interface(void)
{
    return &apn_interface;
}

static bool apn_init_impl(const apn_config_t* config)
{
    if (module_initialized) {
        ESP_LOGW(TAG, "APN manager already initialized");
        return true;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "APN configuration is NULL");
        return false;
    }
    
    // Copy configuration
    memcpy(&current_config, config, sizeof(apn_config_t));
    
    // Initialize status
    memset(&module_status, 0, sizeof(apn_status_t));
    
    if (current_config.debug) {
        ESP_LOGI(TAG, "=== APN MANAGER INITIALIZATION ===");
        ESP_LOGI(TAG, "Default APN: %s", current_config.apn);
        ESP_LOGI(TAG, "Persistence: %s", current_config.persistence ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "Auto-detect: %s", current_config.auto_detect ? "Enabled" : "Disabled");
    }
    
    // Load from NVS if persistence enabled
    if (current_config.persistence) {
        if (apn_load_from_nvs_impl()) {
            ESP_LOGI(TAG, "APN configuration loaded from NVS: %s", current_config.apn);
        } else if (current_config.debug) {
            ESP_LOGI(TAG, "No saved APN configuration, using default");
        }
    }
    
    // Check current modem configuration
    apn_status_t status;
    if (apn_check_configuration_impl(&status)) {
        if (status.is_configured) {
            ESP_LOGI(TAG, "APN already configured on modem: %s", status.current_apn);
            
            // Verify it matches our desired APN
            if (strcmp(status.current_apn, current_config.apn) == 0) {
                ESP_LOGI(TAG, "Existing APN configuration matches desired APN - no action needed");
                module_status = status;
                module_initialized = true;
                return true;
            } else {
                ESP_LOGW(TAG, "Existing APN (%s) differs from desired (%s)", 
                         status.current_apn, current_config.apn);
            }
        }
    }
    
    module_initialized = true;
    ESP_LOGI(TAG, "APN manager initialized successfully");
    return true;
}

static bool apn_deinit_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    // Save to NVS if persistence enabled
    if (current_config.persistence && module_status.is_configured) {
        apn_save_to_nvs_impl();
    }
    
    module_initialized = false;
    ESP_LOGI(TAG, "APN manager deinitialized");
    return true;
}

static bool apn_check_configuration_impl(apn_status_t* status)
{
    if (!status) {
        return false;
    }
    
    memset(status, 0, sizeof(apn_status_t));
    
    // Query current APN configuration from modem
    if (!query_modem_apn_config(status->current_apn, sizeof(status->current_apn))) {
        if (current_config.debug) {
            ESP_LOGW(TAG, "Failed to query modem APN configuration");
        }
        return false;
    }
    
    // Check if APN is configured
    status->is_configured = (strlen(status->current_apn) > 0);
    
    // Check PDP context status
    status->is_active = query_pdp_context_status();
    
    // Get IP address if active
    if (status->is_active) {
        apn_get_ip_address_impl(status->ip_address, sizeof(status->ip_address));
    }
    
    status->config_time = module_status.config_time;
    
    if (current_config.debug) {
        ESP_LOGI(TAG, "APN Status - Configured: %s, Active: %s, APN: %s, IP: %s",
                 status->is_configured ? "YES" : "NO",
                 status->is_active ? "YES" : "NO",
                 status->current_apn,
                 status->ip_address);
    }
    
    return true;
}

static bool apn_set_apn_impl(const char* apn, const char* username, const char* password)
{
    if (!module_initialized || !apn) {
        return false;
    }
    
    // Check if already configured with same APN
    apn_status_t status;
    if (apn_check_configuration_impl(&status)) {
        if (status.is_configured && strcmp(status.current_apn, apn) == 0) {
            ESP_LOGI(TAG, "APN '%s' already configured - skipping set operation", apn);
            return true;
        }
    }
    
    ESP_LOGI(TAG, "Setting APN: %s", apn);
    
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->set_apn) {
        ESP_LOGE(TAG, "LTE interface not available");
        return false;
    }
    
    // Set APN using LTE module
    bool success = lte->set_apn(apn, username, password);
    
    if (success) {
        // Update our configuration
        strncpy(current_config.apn, apn, sizeof(current_config.apn) - 1);
        current_config.apn[sizeof(current_config.apn) - 1] = '\0';
        
        if (username) {
            strncpy(current_config.username, username, sizeof(current_config.username) - 1);
            current_config.username[sizeof(current_config.username) - 1] = '\0';
        }
        
        if (password) {
            strncpy(current_config.password, password, sizeof(current_config.password) - 1);
            current_config.password[sizeof(current_config.password) - 1] = '\0';
        }
        
        // Update status
        module_status.is_configured = true;
        strncpy(module_status.current_apn, apn, sizeof(module_status.current_apn) - 1);
        module_status.current_apn[sizeof(module_status.current_apn) - 1] = '\0';
        module_status.config_time = get_timestamp_ms();
        
        // Save to NVS if persistence enabled
        if (current_config.persistence) {
            apn_save_to_nvs_impl();
        }
        
        ESP_LOGI(TAG, "APN set successfully: %s", apn);
    } else {
        ESP_LOGE(TAG, "Failed to set APN: %s", apn);
    }
    
    return success;
}

static bool apn_activate_context_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    // Check if already active
    if (query_pdp_context_status()) {
        ESP_LOGI(TAG, "PDP context already active");
        module_status.is_active = true;
        return true;
    }
    
    ESP_LOGI(TAG, "Activating PDP context...");
    
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE interface not available");
        return false;
    }
    
    // Activate PDP context
    at_response_t response;
    bool success = lte->send_at_command("AT+CGACT=1,1", &response, 15000);
    
    if (success) {
        module_status.is_active = true;
        ESP_LOGI(TAG, "PDP context activated successfully");
        
        // Get IP address
        apn_get_ip_address_impl(module_status.ip_address, sizeof(module_status.ip_address));
    } else {
        ESP_LOGE(TAG, "Failed to activate PDP context");
    }
    
    return success;
}

static bool apn_deactivate_context_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    ESP_LOGI(TAG, "Deactivating PDP context...");
    
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        ESP_LOGE(TAG, "LTE interface not available");
        return false;
    }
    
    // Deactivate PDP context
    at_response_t response;
    bool success = lte->send_at_command("AT+CGACT=0,1", &response, 10000);
    
    if (success) {
        module_status.is_active = false;
        memset(module_status.ip_address, 0, sizeof(module_status.ip_address));
        ESP_LOGI(TAG, "PDP context deactivated successfully");
    } else {
        ESP_LOGE(TAG, "Failed to deactivate PDP context");
    }
    
    return success;
}

static bool apn_get_status_impl(apn_status_t* status)
{
    if (!module_initialized || !status) {
        return false;
    }
    
    // Update current status from modem
    return apn_check_configuration_impl(status);
}

static bool apn_get_ip_address_impl(char* ip_buffer, size_t buffer_size)
{
    if (!module_initialized || !ip_buffer || buffer_size == 0) {
        return false;
    }
    
    // Clear buffer
    memset(ip_buffer, 0, buffer_size);
    
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        return false;
    }
    
    // Query IP address
    at_response_t response;
    if (!lte->send_at_command("AT+CGPADDR=1", &response, 5000)) {
        return false;
    }
    
    // Parse response: +CGPADDR: 1,<IP address>
    char* ip_start = strstr(response.response, "+CGPADDR: 1,");
    if (!ip_start) {
        return false;
    }
    
    ip_start += 12; // Skip "+CGPADDR: 1,"
    
    // Find end of IP address
    char* ip_end = strchr(ip_start, '\r');
    if (!ip_end) {
        ip_end = strchr(ip_start, '\n');
    }
    if (!ip_end) {
        ip_end = ip_start + strlen(ip_start);
    }
    
    // Copy IP address
    size_t ip_len = ip_end - ip_start;
    if (ip_len >= buffer_size) {
        ip_len = buffer_size - 1;
    }
    
    strncpy(ip_buffer, ip_start, ip_len);
    ip_buffer[ip_len] = '\0';
    
    // Update cached IP
    strncpy(module_status.ip_address, ip_buffer, sizeof(module_status.ip_address) - 1);
    module_status.ip_address[sizeof(module_status.ip_address) - 1] = '\0';
    
    return (strlen(ip_buffer) > 0);
}

static bool apn_is_ready_for_data_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    apn_status_t status;
    if (!apn_get_status_impl(&status)) {
        return false;
    }
    
    return (status.is_configured && status.is_active && strlen(status.ip_address) > 0);
}

static bool apn_save_to_nvs_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(APN_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    bool success = true;
    
    // Save APN
    err = nvs_set_str(nvs_handle, APN_NVS_KEY_APN, current_config.apn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save APN to NVS: %s", esp_err_to_name(err));
        success = false;
    }
    
    // Save username
    if (success && strlen(current_config.username) > 0) {
        err = nvs_set_str(nvs_handle, APN_NVS_KEY_USER, current_config.username);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save username to NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save password
    if (success && strlen(current_config.password) > 0) {
        err = nvs_set_str(nvs_handle, APN_NVS_KEY_PASS, current_config.password);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save password to NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save timestamp
    if (success) {
        err = nvs_set_u32(nvs_handle, APN_NVS_KEY_TIME, module_status.config_time);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save timestamp to NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Commit changes
    if (success) {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    nvs_close(nvs_handle);
    
    if (success && current_config.debug) {
        ESP_LOGI(TAG, "APN configuration saved to NVS");
    }
    
    return success;
}

static bool apn_load_from_nvs_impl(void)
{
    if (!module_initialized) {
        return false;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(APN_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND && current_config.debug) {
            ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        }
        return false;
    }
    
    bool success = false;
    
    // Load APN
    size_t required_size = sizeof(current_config.apn);
    err = nvs_get_str(nvs_handle, APN_NVS_KEY_APN, current_config.apn, &required_size);
    if (err == ESP_OK) {
        success = true;
        
        // Load username (optional)
        required_size = sizeof(current_config.username);
        nvs_get_str(nvs_handle, APN_NVS_KEY_USER, current_config.username, &required_size);
        
        // Load password (optional)
        required_size = sizeof(current_config.password);
        nvs_get_str(nvs_handle, APN_NVS_KEY_PASS, current_config.password, &required_size);
        
        // Load timestamp
        nvs_get_u32(nvs_handle, APN_NVS_KEY_TIME, &module_status.config_time);
    }
    
    nvs_close(nvs_handle);
    return success;
}

static void apn_set_debug_impl(bool enable)
{
    current_config.debug = enable;
    ESP_LOGI(TAG, "Debug mode %s", enable ? "enabled" : "disabled");
}

// Helper function implementations

static bool query_modem_apn_config(char* apn_buffer, size_t buffer_size)
{
    if (!apn_buffer || buffer_size == 0) {
        return false;
    }
    
    memset(apn_buffer, 0, buffer_size);
    
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        return false;
    }
    
    // Query PDP context configuration
    at_response_t response;
    if (!lte->send_at_command("AT+CGDCONT?", &response, 5000)) {
        return false;
    }
    
    // Parse response: +CGDCONT: 1,"IP","<APN>",...
    char* apn_line = strstr(response.response, "+CGDCONT: 1,\"IP\",\"");
    if (!apn_line) {
        return false;
    }
    
    apn_line += 18; // Skip "+CGDCONT: 1,\"IP\",\""
    
    // Find end quote
    char* apn_end = strchr(apn_line, '"');
    if (!apn_end) {
        return false;
    }
    
    // Copy APN name
    size_t apn_len = apn_end - apn_line;
    if (apn_len >= buffer_size) {
        apn_len = buffer_size - 1;
    }
    
    strncpy(apn_buffer, apn_line, apn_len);
    apn_buffer[apn_len] = '\0';
    
    return (strlen(apn_buffer) > 0);
}

static bool query_pdp_context_status(void)
{
    // Get LTE interface
    const lte_interface_t* lte = lte_get_interface();
    if (!lte || !lte->send_at_command) {
        return false;
    }
    
    // Query PDP context status
    at_response_t response;
    if (!lte->send_at_command("AT+CGACT?", &response, 5000)) {
        return false;
    }
    
    // Look for +CGACT: 1,1 (context 1 active)
    return (strstr(response.response, "+CGACT: 1,1") != NULL);
}

static uint32_t get_timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}