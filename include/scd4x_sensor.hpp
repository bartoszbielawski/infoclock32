#pragma once

// Sensirion SCD4x (SCD41/43) / SCD40 / SCD30 NDIR CO2 sensor.
// I2C measurement of CO2 concentration, temperature and relative humidity.
// Fixed I2C address: 0x62 (all SCD4x models).
//
// Config key: temp_sensor=scd4x
//
// Reference library: SensirionI2cScd4x (Sensirion, arduino)
// https://github.com/Sensirion/arduino-i2c-scd4x
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <sensor.hpp>

// SCD4x family I2C address (fixed for all models: SCD40, SCD41, SCD42, SCD43)
static const uint8_t SCD4X_I2C_ADDR = 0x62;

/**
 * Scd4xSensor — Sensirion SCD4x CO2 sensor implementation.
 *
 * Provides:
 *   - CO2 concentration (ppm)
 *   - Temperature (°C)
 *   - Relative humidity (RH%)
 *
 * NDIR sensor with temperature and humidity sideband data used for
 * ambient compensation.
 *
 * Important timing requirements:
 *   - Minimum ~3 s after begin()/wakeUp()/stopPeriodicMeasurement()
 *     before the first readMeasurement() may succeed
 *   - The measurement interval (default 5 s) is enforced by the library's
 *     startPeriodicMeasurement(); read() polls it and only returns true
 *     when a fresh result is available
 */
class Scd4xSensor : public Sensor {
public:
    explicit Scd4xSensor();

    ~Scd4xSensor() override;

    bool begin() override;
    bool read() override;

    float temperature() const override;
    bool hasHumidity() const override { return true; }
    float humidity() const override;

    bool hasPressure() const override { return false; }   // SCD4x does not measure pressure
    float pressure() const override { return 0.0f; }

    bool hasCo2() const override { return true; }
    int16_t co2Concentration() const override;

    const char* name() const override;

private:
    SensirionI2cScd4x _scd;   // owns the I2C driver for the fixed address

    int16_t _co2 = 0;         // ppm, valid after read()
    float   _temp = 0.0f;     // °C, valid after read()
    float   _hum = 0.0f;      // %RH, valid after read()
};