#include "tracker.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "BATTERY";

#define I2C_MASTER_SCL_IO 2
#define I2C_MASTER_SDA_IO 3
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define MAX17048_ADDR 0x36
#define MAX17048_SOC_REG 0x02
#define MAX17048_VCELL_REG 0x04
#define MAX17048_CONFIG_REG 0x0C

static bool battery_initialized = false;

bool battery_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C");
        return false;
    }
    
    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver");
        return false;
    }
    
    battery_initialized = true;
    ESP_LOGI(TAG, "Battery monitor initialized");
    return true;
}

static esp_err_t max17048_read_register(uint8_t reg, uint16_t* value)
{
    if (!battery_initialized) {
        return ESP_FAIL;
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
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        *value = (data[0] << 8) | data[1];
    }
    
    return ret;
}

bool battery_read_data(battery_data_t *data)
{
    if (!battery_initialized || !data) {
        return false;
    }
    
    uint16_t soc_raw, vcell_raw;
    
    // Read State of Charge (SOC)
    if (max17048_read_register(MAX17048_SOC_REG, &soc_raw) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SOC register");
        return false;
    }
    
    // Read Cell Voltage
    if (max17048_read_register(MAX17048_VCELL_REG, &vcell_raw) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read VCELL register");
        return false;
    }
    
    // Convert SOC to percentage (LSB = 1/256 %)
    data->percentage = (float)soc_raw / 256.0f;
    
    // Convert voltage (LSB = 78.125 µV)
    data->voltage = (float)vcell_raw * 78.125e-6f;
    
    // Simple charging detection (voltage > 4.0V typically indicates charging)
    data->charging = (data->voltage > 4.0f);
    
    // Clamp percentage to reasonable range
    if (data->percentage > 100.0f) data->percentage = 100.0f;
    if (data->percentage < 0.0f) data->percentage = 0.0f;
    
    ESP_LOGI(TAG, "Battery: %.1f%%, %.2fV, %s", 
             data->percentage, data->voltage, 
             data->charging ? "charging" : "not charging");
    
    return true;
}