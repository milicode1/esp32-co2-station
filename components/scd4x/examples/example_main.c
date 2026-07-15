#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scd4x.h"
#include <stdio.h>

static const char *TAG = "main";

enum
{
    I2C_MASTER_SCL_IO         = GPIO_NUM_26, /*!< GPIO number for I2C master clock */
    I2C_MASTER_SDA_IO         = GPIO_NUM_25, /*!< GPIO number for I2C master data  */
    I2C_MASTER_NUM            = I2C_NUM_0,   /*!< I2C port number for master dev */
    I2C_MASTER_FREQ_HZ        = 100000,      /*!< I2C master clock frequency */
    I2C_MASTER_TX_BUF_DISABLE = 0,           /*!< I2C master doesn't need buffer */
    I2C_MASTER_RX_BUF_DISABLE = 0            /*!< I2C master doesn't need buffer */
};

static esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port                     = I2C_MASTER_NUM,
        .sda_io_num                   = I2C_MASTER_SDA_IO,
        .scl_io_num                   = I2C_MASTER_SCL_IO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SCD4X_I2C_ADDR,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2C...");
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    esp_err_t               ret = i2c_master_init(&bus_handle, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C initialized successfully");

    ESP_LOGI(TAG, "Initializing SCD4X...");
    scd4x_t *scd4x = scd4x_init(dev_handle);
    if (!scd4x) {
        ESP_LOGE(TAG, "SCD4X initialization failed");
        return;
    }
    ESP_LOGI(TAG, "SCD4X initialized successfully");

    // Read and display serial number
    uint16_t serial[3];
    ret = scd4x_get_serial_number(scd4x, serial);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SCD4X serial: 0x%04X%04X%04X", serial[0], serial[1], serial[2]);
    } else {
        ESP_LOGW(TAG, "Failed to read serial number: %s", esp_err_to_name(ret));
    }

    // Read current temperature offset
    float temp_offset;
    ret = scd4x_get_temperature_offset(scd4x, &temp_offset);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current temperature offset: %.2f C", temp_offset);
    }

    // Read current sensor altitude
    uint16_t altitude;
    ret = scd4x_get_sensor_altitude(scd4x, &altitude);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current sensor altitude: %u m", altitude);
    }

    // Check automatic self-calibration status
    bool asc_enabled;
    ret = scd4x_get_automatic_self_calibration(scd4x, &asc_enabled);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Automatic self-calibration: %s", asc_enabled ? "enabled" : "disabled");
    }

    ret = scd4x_start_periodic_measurement(scd4x);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic measurement: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SCD4X periodic measurement started");

    while (true) {
        scd4x_measurement_t measurement = {0};

        ret = scd4x_read_measurement(scd4x, &measurement);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SCD4X measurement: CO2=%.0f ppm, Temperature=%.2f C, Humidity=%.2f %%RH", measurement.co2, measurement.temperature,
                     measurement.humidity);
        } else if (ret != ESP_ERR_NOT_FINISHED) {
            ESP_LOGE(TAG, "Failed to read SCD4X measurement: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
