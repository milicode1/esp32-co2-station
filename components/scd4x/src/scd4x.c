#include "scd4x.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "scd4x";

enum
{
    /* 3.6 Basic Commands */
    SCD4X_CMD_START_PERIODIC_MEASUREMENT                     = 0x21B1,
    SCD4X_CMD_READ_MEASUREMENT                               = 0xEC05,
    SCD4X_CMD_STOP_PERIODIC_MEASUREMENT                      = 0x3F86,

    /* 3.7 On-chip output signal compensation */
    SCD4X_CMD_SET_TEMPERATURE_OFFSET                         = 0x241D,
    SCD4X_CMD_GET_TEMPERATURE_OFFSET                         = 0x2318,
    SCD4X_CMD_SET_SENSOR_ALTITUDE                            = 0x2427,
    SCD4X_CMD_GET_SENSOR_ALTITUDE                            = 0x2322,
    SCD4X_CMD_SET_AMBIENT_PRESSURE                           = 0xE000,

    /* 3.8 Field Calibration */
    SCD4X_CMD_PERFORM_FORCED_RECALIBRATION                   = 0x362F,
    SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED         = 0x2416,
    SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED         = 0x2313,
    SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_TARGET          = 0x243A,
    SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_TARGET          = 0x233F,

    /* 3.9 Low Power Periodic Measurement Mode */
    SCD4X_CMD_START_LOW_POWER_PERIODIC_MEASUREMENT           = 0x21AC,
    SCD4X_CMD_GET_DATA_READY_STATUS                          = 0xE4B8,

    /* 3.10 Advanced Features */
    SCD4X_CMD_PERSIST_SETTINGS                               = 0x3615,
    SCD4X_CMD_GET_SERIAL_NUMBER                              = 0x3682,
    SCD4X_CMD_PERFORM_SELF_TEST                              = 0x3639,
    SCD4X_CMD_PERFORM_FACTORY_RESET                          = 0x3632,
    SCD4X_CMD_REINIT                                         = 0x3646,
    SCD4X_CMD_GET_SENSOR_VARIANT                             = 0x202F,

    /* 3.11 Single Shot Measurement Mode (SCD41 & SCD43) */
    SCD4X_CMD_MEASURE_SINGLE_SHOT                            = 0x219D,
    SCD4X_CMD_MEASURE_SINGLE_SHOT_RHT_ONLY                   = 0x2196,
    SCD4X_CMD_POWER_DOWN                                     = 0x36E0,
    SCD4X_CMD_WAKE_UP                                        = 0x36F6,
    SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD  = 0x2445,
    SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD  = 0x2340,
    SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD = 0x244E,
    SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD = 0x234B,
};

static uint8_t scd4x_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static void scd4x_wait_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static esp_err_t scd4x_recover_error(scd4x_t *dev);

static esp_err_t scd4x_write_nr(scd4x_t *dev, const uint8_t *buffer, size_t length)
{
    return i2c_master_transmit(dev->i2c_dev, buffer, length, SCD4X_I2C_TIMEOUT_MS);
}

static esp_err_t scd4x_write_command_nr(scd4x_t *dev, uint16_t command)
{
    uint8_t SCD4X_CMD_buffer[2] = //
        {
            command >> 8,  //
            command & 0xFF //
        }; // Command is 2 bytes, split into high and low
    return scd4x_write_nr(dev, SCD4X_CMD_buffer, sizeof(SCD4X_CMD_buffer));
}

static esp_err_t scd4x_transmit_with_recovery(scd4x_t *dev, const uint8_t *buffer, size_t length)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = scd4x_write_nr(dev, buffer, length);
    if (ret == ESP_OK) {
        dev->has_error = false;
        return ret;
    }

    dev->has_error = true;
    ESP_LOGW(TAG, "Write transfer failed: %s. Trying to recover.", esp_err_to_name(ret));

    esp_err_t recover_ret = scd4x_recover_error(dev);
    if (recover_ret != ESP_OK) {
        return recover_ret;
    }

    ret = scd4x_write_nr(dev, buffer, length);
    if (ret == ESP_OK) {
        dev->has_error = false;
    }
    return ret;
}

static esp_err_t scd4x_write_command(scd4x_t *dev, uint16_t command)
{
    uint8_t buffer[2] = {command >> 8, command & 0xFF};
    return scd4x_transmit_with_recovery(dev, buffer, sizeof(buffer));
}

static esp_err_t scd4x_read_data(scd4x_t *dev, uint8_t *data, size_t length)
{
    if (!dev || !data || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dev->has_error) {
        return ESP_ERR_INVALID_STATE; // Don't attempt to read if we're in an error state, force recovery first
    }
    esp_err_t ret = i2c_master_receive(dev->i2c_dev, data, length, SCD4X_I2C_TIMEOUT_MS); // Send repeated start
    return ret;
}

static esp_err_t scd4x_recover_error(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    scd4x_wait_ms(1000); // sleep for 1 second to allow device to initialize or recover from transient issues
    // Wake-up is only meaningful if the sensor is sleeping; if it is already awake
    // or measuring, continue with the recovery sequence anyway.
    esp_err_t ret = scd4x_write_command_nr(dev, SCD4X_CMD_WAKE_UP);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Wake up skipped during recovery: %s", esp_err_to_name(ret));
    }
    scd4x_wait_ms(30);
    ret = scd4x_write_command_nr(dev, SCD4X_CMD_STOP_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop periodic measurement: %s", esp_err_to_name(ret));
        return ret;
    }
    scd4x_wait_ms(500);
    dev->has_error = false; // Clear error state after recovery attempt
    switch (dev->mode) {
    case SCD4X_MODE_PERIODIC:
        ret = scd4x_write_command_nr(dev, SCD4X_CMD_START_PERIODIC_MEASUREMENT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart periodic measurement: %s", esp_err_to_name(ret));
            return ret;
        }
        break;
    case SCD4X_MODE_LOW_POWER_PERIODIC:
        ret = scd4x_write_command_nr(dev, SCD4X_CMD_START_LOW_POWER_PERIODIC_MEASUREMENT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart low-power periodic measurement: %s", esp_err_to_name(ret));
            return ret;
        }
        break;
    case SCD4X_MODE_IDLE:
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t scd4x_write_command_and_value(scd4x_t *dev, uint16_t command, uint16_t value)
{
    uint8_t buffer[5] = {
        command >> 8, command & 0xFF, value >> 8, value & 0xFF, 0,
    };
    buffer[4] = scd4x_crc8(&buffer[2], 2);
    return scd4x_transmit_with_recovery(dev, buffer, sizeof(buffer));
}

static esp_err_t scd4x_read_words(scd4x_t *dev, uint16_t command, uint16_t *values, size_t word_count, int delay_after_cmd)
{
    if (!dev || !values || word_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = scd4x_write_command(dev, command);
    if (ret != ESP_OK) {
        return ret;
    }

    scd4x_wait_ms(delay_after_cmd);

    uint8_t data[word_count * 3];
    ret = scd4x_read_data(dev, data, sizeof(data));
    if (ret != ESP_OK) {
        dev->has_error = true;
        return ret;
    }

    for (size_t i = 0; i < word_count; i++) {
        uint8_t crc = scd4x_crc8(&data[i * 3], 2);
        if (crc != data[i * 3 + 2]) {
            ESP_LOGE(TAG, "CRC mismatch: calculated 0x%02X, received 0x%02X", crc, data[i * 3 + 2]);
            dev->has_error = true;
            return ESP_ERR_INVALID_CRC;
        }
        values[i] = ((uint16_t)data[i * 3] << 8) | data[i * 3 + 1];
    }

    return ESP_OK;
}

static esp_err_t scd4x_read_word(scd4x_t *dev, uint16_t command, uint16_t *value)
{
    return scd4x_read_words(dev, command, value, 1, 1);
}

esp_err_t scd4x_start_periodic_measurement(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_START_PERIODIC_MEASUREMENT);
    if (ret == ESP_OK) {
        dev->mode = SCD4X_MODE_PERIODIC;
    }
    return ret;
}

esp_err_t scd4x_stop_periodic_measurement(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_STOP_PERIODIC_MEASUREMENT);
    if (ret == ESP_OK) {
        dev->mode = SCD4X_MODE_IDLE;
        scd4x_wait_ms(500);
    }
    return ret;
}

esp_err_t scd4x_set_temperature_offset(scd4x_t *dev, float offset_c)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if (offset_c < 0.0f || offset_c > 175.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t raw = (uint16_t)(offset_c * 65535.0f / 175.0f + 0.5f);
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_TEMPERATURE_OFFSET, raw);
}

esp_err_t scd4x_get_temperature_offset(scd4x_t *dev, float *offset_c)
{
    if (!dev || !offset_c) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t  raw;
    esp_err_t ret = scd4x_read_word(dev, SCD4X_CMD_GET_TEMPERATURE_OFFSET, &raw);
    if (ret != ESP_OK) {
        return ret;
    }
    *offset_c = raw * 175.0f / 65535.0f;
    return ESP_OK;
}

esp_err_t scd4x_set_sensor_altitude(scd4x_t *dev, uint16_t altitude_m)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_SENSOR_ALTITUDE, altitude_m);
}

esp_err_t scd4x_get_sensor_altitude(scd4x_t *dev, uint16_t *altitude_m)
{
    if (!dev || !altitude_m) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_word(dev, SCD4X_CMD_GET_SENSOR_ALTITUDE, altitude_m);
}

esp_err_t scd4x_set_ambient_pressure(scd4x_t *dev, uint16_t pressure_hpa)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pressure_hpa < 700 || pressure_hpa > 1200) {
        return ESP_ERR_INVALID_ARG;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_AMBIENT_PRESSURE, pressure_hpa);
}

esp_err_t scd4x_get_ambient_pressure(scd4x_t *dev, uint16_t *pressure_hpa)
{
    if (!dev || !pressure_hpa) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_word(dev, SCD4X_CMD_SET_AMBIENT_PRESSURE, pressure_hpa);
}

esp_err_t scd4x_data_ready(scd4x_t *dev, bool *ready)
{
    if (!dev || !ready) {
        return ESP_ERR_INVALID_STATE;
    }
    *ready           = false;
    uint16_t  status = 0;
    esp_err_t ret    = scd4x_read_word(dev, SCD4X_CMD_GET_DATA_READY_STATUS, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    *ready = (status & 0x07FF) != 0;
    return ESP_OK;
}

esp_err_t scd4x_read_measurement(scd4x_t *dev, scd4x_measurement_t *measurement)
{
    if (!dev || !measurement) {
        return ESP_ERR_INVALID_STATE;
    }
    bool      data_ready;
    esp_err_t ret = scd4x_data_ready(dev, &data_ready);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!data_ready) {
        return ESP_ERR_NOT_FINISHED;
    }
    uint16_t words[3];
    ret = scd4x_read_words(dev, SCD4X_CMD_READ_MEASUREMENT, words, 3, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    measurement->co2         = words[0];
    measurement->temperature = words[1] * 175.0f / 65535.0f - 45.0f;
    measurement->humidity    = words[2] * 100.0f / 65535.0f;
    return ESP_OK;
}

esp_err_t scd4x_perform_forced_recalibration(scd4x_t *dev, uint16_t target_co2_ppm, int16_t *frc_correction)
{
    if (!dev || !frc_correction) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command_and_value(dev, SCD4X_CMD_PERFORM_FORCED_RECALIBRATION, target_co2_ppm);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(400);
    uint8_t data[3];
    ret = scd4x_read_data(dev, data, sizeof(data));
    if (ret != ESP_OK) {
        dev->has_error = true;
        return ret;
    }
    uint8_t crc = scd4x_crc8(data, 2);
    if (crc != data[2]) {
        ESP_LOGE(TAG, "CRC mismatch in FRC response");
        dev->has_error = true;
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t word = ((uint16_t)data[0] << 8) | data[1];
    if (word == 0xFFFF) {
        ESP_LOGE(TAG, "Forced recalibration failed");
        return ESP_ERR_INVALID_RESPONSE;
    }
    *frc_correction = (int16_t)(word - 0x8000);
    return ESP_OK;
}

esp_err_t scd4x_set_automatic_self_calibration(scd4x_t *dev, bool enabled)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_ENABLED, enabled ? 1 : 0);
}

esp_err_t scd4x_get_automatic_self_calibration(scd4x_t *dev, bool *enabled)
{
    if (!dev || !enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t  raw;
    esp_err_t ret = scd4x_read_word(dev, SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_ENABLED, &raw);
    if (ret != ESP_OK) {
        return ret;
    }
    *enabled = raw != 0;
    return ESP_OK;
}

esp_err_t scd4x_set_automatic_self_calibration_target(scd4x_t *dev, uint16_t ppm)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_TARGET, ppm);
}

esp_err_t scd4x_get_automatic_self_calibration_target(scd4x_t *dev, uint16_t *ppm)
{
    if (!dev || !ppm) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_word(dev, SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_TARGET, ppm);
}

esp_err_t scd4x_set_automatic_self_calibration_initial_period(scd4x_t *dev, uint16_t hours)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((hours % 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD, hours);
}

esp_err_t scd4x_get_automatic_self_calibration_initial_period(scd4x_t *dev, uint16_t *hours)
{
    if (!dev || !hours) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_word(dev, SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_INITIAL_PERIOD, hours);
}

esp_err_t scd4x_set_automatic_self_calibration_standard_period(scd4x_t *dev, uint16_t hours)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((hours % 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return scd4x_write_command_and_value(dev, SCD4X_CMD_SET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD, hours);
}

esp_err_t scd4x_get_automatic_self_calibration_standard_period(scd4x_t *dev, uint16_t *hours)
{
    if (!dev || !hours) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_word(dev, SCD4X_CMD_GET_AUTOMATIC_SELF_CALIBRATION_STANDARD_PERIOD, hours);
}

esp_err_t scd4x_start_low_power_periodic_measurement(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_START_LOW_POWER_PERIODIC_MEASUREMENT);
    if (ret == ESP_OK) {
        dev->mode = SCD4X_MODE_LOW_POWER_PERIODIC;
    }
    return ret;
}

esp_err_t scd4x_persist_settings(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_PERSIST_SETTINGS);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(800);
    return ESP_OK;
}

esp_err_t scd4x_get_serial_number(scd4x_t *dev, uint16_t serial[3])
{
    if (!dev || !serial) {
        return ESP_ERR_INVALID_STATE;
    }
    return scd4x_read_words(dev, SCD4X_CMD_GET_SERIAL_NUMBER, serial, 3, 1);
}

esp_err_t scd4x_perform_self_test(scd4x_t *dev, bool *malfunction)
{
    if (!dev || !malfunction) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t  result;
    esp_err_t ret = scd4x_read_words(dev, SCD4X_CMD_PERFORM_SELF_TEST, &result, 1, 10000);
    if (ret != ESP_OK) {
        return ret;
    }
    *malfunction = result != 0;
    return ESP_OK;
}

esp_err_t scd4x_perform_factory_reset(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_PERFORM_FACTORY_RESET);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(1200);
    dev->mode = SCD4X_MODE_IDLE;
    return ESP_OK;
}

esp_err_t scd4x_reinit(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_REINIT);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(30);
    dev->mode = SCD4X_MODE_IDLE;
    return ESP_OK;
}

esp_err_t scd4x_get_sensor_variant(scd4x_t *dev, scd4x_variant_t *variant)
{
    if (!dev || !variant) {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t  raw;
    esp_err_t ret = scd4x_read_word(dev, SCD4X_CMD_GET_SENSOR_VARIANT, &raw);
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t code = (raw >> 12) & 0x0F;
    switch (code) {
    case SCD4X_VARIANT_SCD40:
    case SCD4X_VARIANT_SCD41:
    case SCD4X_VARIANT_SCD43:
        *variant = (scd4x_variant_t)code;
        break;
    default:
        *variant = SCD4X_VARIANT_UNKNOWN;
        break;
    }
    return ESP_OK;
}

esp_err_t scd4x_measure_single_shot(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_MEASURE_SINGLE_SHOT);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(5000);
    return ESP_OK;
}

esp_err_t scd4x_measure_single_shot_rht_only(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_MEASURE_SINGLE_SHOT_RHT_ONLY);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(50);
    return ESP_OK;
}

esp_err_t scd4x_power_down(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = scd4x_write_command(dev, SCD4X_CMD_POWER_DOWN);
    if (ret != ESP_OK) {
        return ret;
    }
    scd4x_wait_ms(1);
    dev->mode = SCD4X_MODE_IDLE;
    return ESP_OK;
}

esp_err_t scd4x_wake_up(scd4x_t *dev)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    // wake_up command may NACK since sensor is asleep; ignore the return value
    scd4x_write_command_nr(dev, SCD4X_CMD_WAKE_UP);
    scd4x_wait_ms(30);
    return ESP_OK;
}

scd4x_t *scd4x_init(i2c_master_dev_handle_t dev_handle)
{
    scd4x_t *dev = malloc(sizeof(scd4x_t));
    if (!dev) {
        ESP_LOGE(TAG, "Failed to allocate memory for SCD4X device");
        return NULL;
    }
    dev->i2c_dev   = dev_handle;
    dev->has_error = false;
    dev->mode      = SCD4X_MODE_IDLE;

    esp_err_t ret = scd4x_stop_periodic_measurement(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop periodic measurement: %s", esp_err_to_name(ret));
        free(dev);
        return NULL;
    }
    scd4x_wait_ms(100);
    return dev;
}
