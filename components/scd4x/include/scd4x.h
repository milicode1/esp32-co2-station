#pragma once
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include <stdbool.h>

typedef enum __attribute__((packed))
{
    SCD4X_MODE_IDLE               = 0, /*!< Sensor is idle (no periodic measurement running) */
    SCD4X_MODE_PERIODIC           = 1, /*!< Periodic measurement (5 s interval) */
    SCD4X_MODE_LOW_POWER_PERIODIC = 2, /*!< Low-power periodic measurement (~30 s interval) */
} scd4x_mode_t;

typedef struct
{
    i2c_master_dev_handle_t i2c_dev;   /*!< I2C master device handle */
    bool                    has_error; /*!< Flag to indicate if the device has encountered an error */
    scd4x_mode_t            mode;      /*!< Last requested measurement mode (used for recovery) */
} scd4x_t;

typedef struct
{
    float co2;         /*!< CO2 concentration in ppm */
    float temperature; /*!< Temperature in degrees Celsius */
    float humidity;    /*!< Relative humidity in %RH */
} scd4x_measurement_t;

typedef enum
{
    SCD4X_VARIANT_SCD40    = 0x0, /*!< SCD40 */
    SCD4X_VARIANT_SCD41    = 0x1, /*!< SCD41 */
    SCD4X_VARIANT_SCD43    = 0x5, /*!< SCD43 */
    SCD4X_VARIANT_UNKNOWN  = 0xF, /*!< Unknown / future variant */
} scd4x_variant_t;

#define SCD4X_I2C_ADDR       0x62
#define SCD4X_I2C_TIMEOUT_MS 1000

/**
 * @brief Initialize the SCD4x sensor and allocate the device handle.
 *
 * Stops any active periodic measurement and waits for the sensor to become idle.
 *
 * @param dev_handle I2C master device handle for the SCD4x (address 0x62)
 * @return Pointer to the allocated scd4x_t device, or NULL on failure
 */
scd4x_t  *scd4x_init(i2c_master_dev_handle_t dev_handle);

/**
 * @brief Read CO2, temperature and humidity from the sensor.
 *
 * Returns ESP_ERR_NOT_FINISHED if no new data is available yet. Only valid
 * during periodic measurement or after a single-shot measurement completes.
 * Max command duration: 1 ms.
 *
 * @param dev       Device handle
 * @param[out] measurement Measurement result (CO2 in ppm, T in °C, RH in %)
 * @return ESP_OK on success
 */
esp_err_t scd4x_read_measurement(scd4x_t *dev, scd4x_measurement_t *measurement);

/**
 * @brief Check whether new measurement data is available for read-out.
 *
 * Max command duration: 1 ms.
 *
 * @param dev        Device handle
 * @param[out] ready true if data is ready, false otherwise
 * @return ESP_OK on success
 */
esp_err_t scd4x_data_ready(scd4x_t *dev, bool *ready);

/**
 * @brief Set the ambient pressure for continuous pressure compensation.
 *
 * Can be sent during periodic measurements. Overrides any altitude-based
 * compensation previously set with scd4x_set_sensor_altitude().
 * Valid range: 700..1200 hPa (datasheet §3.7.5). Default is 1013 hPa.
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param pressure_hpa Ambient pressure in hPa (= mbar)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range
 */
esp_err_t scd4x_set_ambient_pressure(scd4x_t *dev, uint16_t pressure_hpa);

/**
 * @brief Read back the ambient pressure currently configured on the sensor.
 *
 * Can be called during periodic measurements. Max command duration: 1 ms.
 *
 * @param dev               Device handle
 * @param[out] pressure_hpa  Ambient pressure in hPa (= mbar)
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_ambient_pressure(scd4x_t *dev, uint16_t *pressure_hpa);

/**
 * @brief Get the sensor altitude stored in the SCD4x.
 *
 * Must be called while the sensor is in idle mode.
 * Max command duration: 1 ms.
 *
 * @param dev            Device handle
 * @param[out] altitude_m Altitude in meters above sea level
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_sensor_altitude(scd4x_t *dev, uint16_t *altitude_m);

/**
 * @brief Set the sensor altitude for pressure compensation.
 *
 * Must be called while the sensor is in idle mode. Use scd4x_persist_settings()
 * to save to EEPROM. Default is 0 m.
 * Max command duration: 1 ms.
 *
 * @param dev        Device handle
 * @param altitude_m Altitude in meters above sea level
 * @return ESP_OK on success
 */
esp_err_t scd4x_set_sensor_altitude(scd4x_t *dev, uint16_t altitude_m);

/**
 * @brief Get the temperature offset currently configured on the sensor.
 *
 * Must be called while the sensor is in idle mode.
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] offset_c Temperature offset in °C
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_temperature_offset(scd4x_t *dev, float *offset_c);

/**
 * @brief Set the temperature offset for RH/T output compensation.
 *
 * Does not affect CO2 accuracy. Must be called while the sensor is in idle mode.
 * Use scd4x_persist_settings() to save to EEPROM. Default is 4 °C.
 * Max command duration: 1 ms.
 *
 * @param dev      Device handle
 * @param offset_c Temperature offset in °C (0..175)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range
 */
esp_err_t scd4x_set_temperature_offset(scd4x_t *dev, float offset_c);

/**
 * @brief Stop periodic measurement and return the sensor to idle mode.
 *
 * Blocks for 500 ms (max command duration) before returning so that the sensor
 * is ready to accept configuration commands.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_stop_periodic_measurement(scd4x_t *dev);

/**
 * @brief Start periodic measurement with a 5-second signal update interval.
 *
 * While running, only read_measurement, data_ready, stop_periodic_measurement
 * and set_ambient_pressure commands are allowed.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_start_periodic_measurement(scd4x_t *dev);

/**
 * @brief Start low-power periodic measurement (~30-second update interval).
 *
 * Reduces power consumption and self-heating at the cost of longer response time.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_start_low_power_periodic_measurement(scd4x_t *dev);

/**
 * @brief Perform forced recalibration (FRC) to a known CO2 reference.
 *
 * The sensor must have been operated in its intended mode for >3 minutes in a
 * stable CO2 environment, then stopped, before calling this function.
 * Max command duration: 400 ms.
 *
 * @param dev             Device handle
 * @param target_co2_ppm  Reference CO2 concentration in ppm
 * @param[out] frc_correction FRC correction value in ppm (positive = upward)
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE if FRC failed
 */
esp_err_t scd4x_perform_forced_recalibration(scd4x_t *dev, uint16_t target_co2_ppm, int16_t *frc_correction);

/**
 * @brief Enable or disable automatic self-calibration (ASC).
 *
 * ASC is enabled by default. The algorithm assumes exposure to 400 ppm at least
 * once per week. Use scd4x_persist_settings() to save to EEPROM.
 * Max command duration: 1 ms.
 *
 * @param dev     Device handle
 * @param enabled true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t scd4x_set_automatic_self_calibration(scd4x_t *dev, bool enabled);

/**
 * @brief Get the current state of automatic self-calibration (ASC).
 *
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] enabled true if ASC is enabled
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_automatic_self_calibration(scd4x_t *dev, bool *enabled);

/**
 * @brief Set the ASC target CO2 concentration in ppm.
 *
 * Defines the reference CO2 concentration (typical outdoor air) that ASC
 * assumes the sensor sees periodically. Default is 400 ppm.
 *
 * Must be called while the sensor is in idle mode. Use scd4x_persist_settings()
 * to save to EEPROM. Max command duration: 1 ms.
 *
 * @param dev      Device handle
 * @param ppm      Target CO2 concentration in ppm
 * @return ESP_OK on success
 */
esp_err_t scd4x_set_automatic_self_calibration_target(scd4x_t *dev, uint16_t ppm);

/**
 * @brief Get the ASC target CO2 concentration in ppm.
 *
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] ppm     Target CO2 concentration in ppm
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_automatic_self_calibration_target(scd4x_t *dev, uint16_t *ppm);

/**
 * @brief Set the ASC initial learning period in hours.
 *
 * Defines the duration the ASC algorithm spends in its initial learning phase
 * after a factory reset or when ASC history has been erased. The value must be
 * a multiple of 4 hours; default is 44 h. Particularly relevant for single-shot
 * operation where the default cadence assumption (one measurement every 5 min)
 * may not match the application.
 *
 * Must be called while the sensor is in idle mode. Use scd4x_persist_settings()
 * to save to EEPROM. Max command duration: 1 ms.
 *
 * @param dev      Device handle
 * @param hours    Initial period in hours (multiple of 4, 0 disables ASC)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if not a multiple of 4
 */
esp_err_t scd4x_set_automatic_self_calibration_initial_period(scd4x_t *dev, uint16_t hours);

/**
 * @brief Get the ASC initial learning period in hours.
 *
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] hours   Initial period in hours
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_automatic_self_calibration_initial_period(scd4x_t *dev, uint16_t *hours);

/**
 * @brief Set the ASC standard learning period in hours.
 *
 * Defines the duration of each subsequent learning phase after the initial one.
 * The value must be a multiple of 4 hours; default is 156 h. Particularly
 * relevant for single-shot operation where measurement cadence differs from
 * the 5-minute default the ASC algorithm assumes.
 *
 * Must be called while the sensor is in idle mode. Use scd4x_persist_settings()
 * to save to EEPROM. Max command duration: 1 ms.
 *
 * @param dev      Device handle
 * @param hours    Standard period in hours (multiple of 4, 0 disables ASC)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if not a multiple of 4
 */
esp_err_t scd4x_set_automatic_self_calibration_standard_period(scd4x_t *dev, uint16_t hours);

/**
 * @brief Get the ASC standard learning period in hours.
 *
 * Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] hours   Standard period in hours
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_automatic_self_calibration_standard_period(scd4x_t *dev, uint16_t *hours);

/**
 * @brief Persist current configuration (temperature offset, altitude, ASC) to EEPROM.
 *
 * EEPROM is guaranteed for at least 2000 write cycles. Only call when actual
 * changes have been made. Max command duration: 800 ms.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_persist_settings(scd4x_t *dev);

/**
 * @brief Read the 48-bit unique serial number of the sensor.
 *
 * Can also be used to verify sensor presence. Max command duration: 1 ms.
 *
 * @param dev          Device handle
 * @param[out] serial  Array of 3 uint16_t words (big-endian, serial = word[0]<<32 | word[1]<<16 | word[2])
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_serial_number(scd4x_t *dev, uint16_t serial[3]);

/**
 * @brief Run the built-in self-test to check sensor functionality.
 *
 * Can be used as an end-of-line test. Max command duration: 10000 ms.
 *
 * @param dev              Device handle
 * @param[out] malfunction true if a malfunction was detected
 * @return ESP_OK on success
 */
esp_err_t scd4x_perform_self_test(scd4x_t *dev, bool *malfunction);

/**
 * @brief Reset all configuration to factory defaults and erase FRC/ASC history.
 *
 * Max command duration: 1200 ms.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_perform_factory_reset(scd4x_t *dev);

/**
 * @brief Reinitialize the sensor by reloading user settings from EEPROM.
 *
 * The sensor must be in idle mode (stop measurement first). If reinit does not
 * achieve the desired result, apply a power-cycle instead.
 * Max command duration: 30 ms.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_reinit(scd4x_t *dev);

/**
 * @brief Read the sensor variant (SCD40, SCD41 or SCD43).
 *
 * Uses the `get_sensor_variant` command (op-code 0x202F). The variant is
 * encoded in bits[15:12] of the response word. Max command duration: 1 ms.
 *
 * @param dev           Device handle
 * @param[out] variant  Decoded sensor variant
 * @return ESP_OK on success
 */
esp_err_t scd4x_get_sensor_variant(scd4x_t *dev, scd4x_variant_t *variant);

/**
 * @brief Trigger a single-shot measurement of CO2, temperature and humidity (SCD41 / SCD43).
 *
 * Blocks for 5000 ms while the measurement completes. Read results with
 * scd4x_read_measurement() afterwards.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_measure_single_shot(scd4x_t *dev);

/**
 * @brief Trigger a single-shot measurement of temperature and humidity only (SCD41 / SCD43).
 *
 * Blocks for 50 ms while the measurement completes. CO2 field in
 * scd4x_read_measurement() output will be 0. Max command duration: 50 ms.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_measure_single_shot_rht_only(scd4x_t *dev);

/**
 * @brief Put the sensor into sleep mode to reduce current consumption.
 *
 * The sensor will not respond to any command other than wake_up while sleeping.
 * Max command duration: 1 ms.
 *
 * @param dev Device handle
 * @return ESP_OK on success
 */
esp_err_t scd4x_power_down(scd4x_t *dev);

/**
 * @brief Wake the sensor from sleep mode entered via scd4x_power_down().
 *
 * The wake-up command may NACK because the sensor is asleep; this is expected.
 * Blocks for 30 ms to allow the sensor to become ready.
 *
 * @param dev Device handle
 * @return ESP_OK (always succeeds; NACK from sleeping sensor is ignored)
 */
esp_err_t scd4x_wake_up(scd4x_t *dev);
