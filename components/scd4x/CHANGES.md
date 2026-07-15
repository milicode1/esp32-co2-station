# Changelog

## 0.0.3

Documentation fix: clarified the SCD40 / SCD41 / SCD43 comparison table in the
README. The shared 0–40 000 ppm CO₂ output range is now listed separately
from each variant's specified accuracy range, and the SCD41's three accuracy
bands are spelled out individually. No code changes.

## 0.0.2

Caught up with the latest SCD4x datasheet (rev. 1.7, April 2025) and taught
the driver about the new SCD43 chip along the way.

Big thanks to **Pierre Lavergne** for pointing out that the following
functions from the official Sensirion API were missing — particularly useful
when running the sensor in idle / single-shot mode:

- `scd4x_get_automatic_self_calibration_target` /
  `scd4x_set_automatic_self_calibration_target`
- `scd4x_get_automatic_self_calibration_initial_period` /
  `scd4x_set_automatic_self_calibration_initial_period`
- `scd4x_get_automatic_self_calibration_standard_period` /
  `scd4x_set_automatic_self_calibration_standard_period`
- `scd4x_get_ambient_pressure`

What's new:

- You can now ask the sensor which variant it is — `scd4x_get_sensor_variant()`
  returns SCD40, SCD41, SCD43, or UNKNOWN.
- Full control over Automatic Self-Calibration: target CO₂, initial period
  and standard period all have getters and setters now. Handy if you're
  running the sensor in single-shot mode.
- Added `scd4x_get_ambient_pressure()` so you can read back whatever pressure
  value the sensor is currently using for compensation.
- The device struct keeps track of the current measurement mode (idle,
  periodic, low-power periodic) via a new packed `scd4x_mode_t` field.
- README got a "Sensor variants" section that compares SCD40, SCD41 and SCD43
  side by side.

What changed:

- `scd4x_init()` no longer takes a bus handle — just the device handle. The
  bus handle was never actually used.
- Error recovery used to always restart regular periodic mode. It now
  remembers which mode you were in and restarts the right one.
- `scd4x_set_ambient_pressure()` rejects values outside 700..1200 hPa
  instead of forwarding garbage to the sensor.
- Bumped the reinit and wake-up delays from 20 to 30 ms to match the new
  datasheet timing.
- Reorganised the internal command list to follow the datasheet section
  layout (§3.6–3.11), which makes it much easier to cross-check.

Cleanup:

- Dropped the unused `bus_handle` field and replaced the `is_measuring`
  bool with the new `mode` enum.

## 0.0.1

Initial release.
