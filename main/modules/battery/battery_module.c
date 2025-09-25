#include "battery_module.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BATTERY_MODULE";

// MAX17048 register definitions
#define MAX17048_ADDR           0x36
#define MAX17048_SOC_REG        0x02
#define MAX17048_VCELL_REG      0x04
#define MAX17048_MODE_REG       0x06
#define MAX17048_VERSION_REG    0x08
#define MAX17048_CONFIG_REG     0x0C
#define MAX17048_COMMAND_REG    0xFE

// Module state
static battery_config_t current_config = {0};
static battery_status_t module_status = {0};
static bool module_initialized = false;
static i2c_config_hw_t hw_config = {0};

// Private function prototypes
static bool battery_init_impl(const battery_config_t* config);
static bool battery_deinit_impl(void);
static bool battery_read_data_impl(battery_data_t* data);
static bool battery_get_status_impl(battery_status_t* status);
static bool battery_calibrate_impl(void);
static bool battery_reset_impl(void);
static void battery_set_debug_impl(bool enable);

// Helper functions
static esp_err_t max17048_read_register(uint8_t reg, uint16_t* value);
static esp_err_t max17048_write_register(uint8_t reg, uint16_t value);
static bool max17048_check_presence(void);
static float convert_soc_to_percentage(uint16_t soc_raw);
static float convert_vcell_to_voltage(uint16_t vcell_raw);

// Battery interface implementation
static const battery_interface_t battery_interface = {
    .init = battery_init_impl,
    .deinit = battery_deinit_impl,
    .read_data = battery_read_data_impl,
    .get_status = battery_get_status_impl,
    .calibrate = battery_calibrate_impl,
    .reset = battery_reset_impl,
    .set_debug = battery_set_debug_impl
};

const battery_interface_t* battery_get_interface(void)
{
    return &battery_interface;
}

static bool battery_init_impl(const battery_config_t* config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return false;
    }
    
    if (module_initialized) {
        ESP_LOGW(TAG, "Battery module already initialized");
        return true;
    }
    
    // Store configuration
    memcpy(&current_config, config, sizeof(battery_config_t));
    
    // Use default I2C configuration
    hw_config.i2c_num = 0;  // I2C_NUM_0
    hw_config.sda_pin = 3;
    hw_config.scl_pin = 2;
    hw_config.frequency_hz = 100000;
    
    // Initialize I2C
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = hw_config.sda_pin,
        .scl_io_num = hw_config.scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = hw_config.frequency_hz,
    };
    
    esp_err_t ret = i2c_param_config(hw_config.i2c_num, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = i2c_driver_install(hw_config.i2c_num, i2c_conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Check if MAX17048 is present
    if (!max17048_check_presence()) {
        ESP_LOGE(TAG, "MAX17048 not detected on I2C bus");
        i2c_driver_delete(hw_config.i2c_num);
        return false;
    }
    
    // Initialize module status
    memset(&module_status, 0, sizeof(module_status));
    module_status.initialized = true;
    module_status.sensor_ready = true;
    
    module_initialized = true;
    
    if (config->debug_output) {
        ESP_LOGI(TAG, "Battery module initialized successfully");
        ESP_LOGI(TAG, "  I2C: SDA=%d, SCL=%d, freq=%d Hz", 
                 hw_config.sda_pin, hw_config.scl_pin, hw_config.frequency_hz);
        ESP_LOGI(TAG, "  Low battery: %.1f%%", config->low_battery_threshold);
        ESP_LOGI(TAG, "  Critical battery: %.1f%%", config->critical_battery_threshold);
        
        // Read and display version
        uint16_t version;
        if (max17048_read_register(MAX17048_VERSION_REG, &version) == ESP_OK) {
            ESP_LOGI(TAG, "  MAX17048 version: 0x%04X", version);
        }
    }
    
    return true;
}

static bool battery_deinit_impl(void)
{
    if (!module_initialized) {
        return true;
    }
    
    i2c_driver_delete(hw_config.i2c_num);
    memset(&module_status, 0, sizeof(module_status));
    module_initialized = false;
    
    ESP_LOGI(TAG, "Battery module deinitialized");
    return true;
}

static bool battery_read_data_impl(battery_data_t* data)
{
    if (!module_initialized || !data) {
        return false;
    }
    
    memset(data, 0, sizeof(battery_data_t));
    
    uint16_t soc_raw, vcell_raw;
    
    // Read State of Charge (SOC)
    if (max17048_read_register(MAX17048_SOC_REG, &soc_raw) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SOC register");
        module_status.read_errors++;
        return false;
    }
    
    // Read Cell Voltage
    if (max17048_read_register(MAX17048_VCELL_REG, &vcell_raw) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read VCELL register");
        module_status.read_errors++;
        return false;
    }
    
    // Convert raw values
    data->percentage = convert_soc_to_percentage(soc_raw);
    data->voltage = convert_vcell_to_voltage(vcell_raw);
    data->present = true; // If we can read, battery is present
    
    // Simple charging detection based on voltage
    data->charging = (data->voltage > 4.0f);
    
    // Clamp percentage to reasonable range
    if (data->percentage > 100.0f) data->percentage = 100.0f;
    if (data->percentage < 0.0f) data->percentage = 0.0f;
    
    // Update status
    module_status.total_reads++;
    module_status.last_read_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Check thresholds
    module_status.low_battery_alert = battery_is_low(data, current_config.low_battery_threshold);
    module_status.critical_battery_alert = battery_is_critical(data, current_config.critical_battery_threshold);
    
    if (current_config.debug_output) {
        ESP_LOGI(TAG, "Battery: %.1f%%, %.2fV, %s%s%s", 
                 data->percentage, data->voltage, 
                 data->charging ? "charging" : "not charging",
                 module_status.low_battery_alert ? ", LOW" : "",
                 module_status.critical_battery_alert ? ", CRITICAL" : "");
    }
    
    return true;
}

static bool battery_get_status_impl(battery_status_t* status)
{
    if (!status) {
        return false;
    }
    
    memcpy(status, &module_status, sizeof(battery_status_t));
    return true;
}

static bool battery_calibrate_impl(void)
{
    ESP_LOGI(TAG, "Battery calibration not implemented for MAX17048");
    return true;
}

static bool battery_reset_impl(void)
{
    ESP_LOGI(TAG, "Resetting battery module statistics");
    
    module_status.total_reads = 0;
    module_status.read_errors = 0;
    module_status.low_battery_alert = false;
    module_status.critical_battery_alert = false;
    
    return true;
}

static void battery_set_debug_impl(bool enable)
{
    current_config.debug_output = enable;
    ESP_LOGI(TAG, "Debug output %s", enable ? "enabled" : "disabled");
}

// Helper function implementations
static esp_err_t max17048_read_register(uint8_t reg, uint16_t* value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t data[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // Write register address
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    
    // Read register value
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(hw_config.i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        *value = (data[0] << 8) | data[1];
    }
    
    return ret;
}

static esp_err_t max17048_write_register(uint8_t reg, uint16_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, (value >> 8) & 0xFF, true);
    i2c_master_write_byte(cmd, value & 0xFF, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(hw_config.i2c_num, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

static bool max17048_check_presence(void)
{
    uint16_t version;
    esp_err_t ret = max17048_read_register(MAX17048_VERSION_REG, &version);
    
    if (ret == ESP_OK) {
        // MAX17048 typically returns 0x0011 or 0x0012 for version
        if ((version & 0xFF00) == 0x0000 || (version & 0xFF00) == 0x0100) {
            return true;
        }
    }
    
    return false;
}

static float convert_soc_to_percentage(uint16_t soc_raw)
{
    // SOC register format: upper byte is percentage, lower byte is 1/256%
    return (float)soc_raw / 256.0f;
}

static float convert_vcell_to_voltage(uint16_t vcell_raw)
{
    // VCELL register LSB = 78.125 µV
    return (float)vcell_raw * 78.125e-6f;
}

// Utility functions
bool battery_is_low(const battery_data_t* data, float threshold)
{
    return data && data->present && data->percentage <= threshold;
}

bool battery_is_critical(const battery_data_t* data, float threshold)
{
    return data && data->present && data->percentage <= threshold;
}

const char* battery_get_status_string(const battery_data_t* data)
{
    if (!data || !data->present) {
        return "Not present";
    }
    
    if (data->percentage <= 5.0f) {
        return "Critical";
    } else if (data->percentage <= 15.0f) {
        return "Low";
    } else if (data->percentage <= 30.0f) {
        return "Fair";
    } else if (data->percentage <= 80.0f) {
        return "Good";
    } else {
        return "Excellent";
    }
}

bool battery_format_info(const battery_data_t* data, char* buffer, size_t buffer_size)
{
    if (!data || !buffer || buffer_size < 64) {
        return false;
    }
    
    snprintf(buffer, buffer_size, "%.1f%% (%.2fV) %s%s",
             data->percentage, data->voltage,
             data->charging ? "Charging" : "Discharging",
             data->present ? "" : " - Not present");
    
    return true;
}