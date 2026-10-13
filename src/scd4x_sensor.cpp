/** SCD4x (Sensirion) CO2 sensor implementation for Infoclock32 **/
#include <Arduino.h>
#include <logger.hpp>
#include "scd4x_sensor.hpp"

Scd4xSensor::Scd4xSensor() = default;

Scd4xSensor::~Scd4xSensor() = default;

bool Scd4xSensor::begin() {
    // The I2C bus (Wire.begin()) is initialized by main.cpp before the
    // factory runs, so it is ready to use here.
    _scd.begin(Wire, SCD4X_I2C_ADDR);

    // Stop any periodic measurement left over from a previous run so we can
    // start from a clean idle state.
    int16_t error = _scd.stopPeriodicMeasurement();
    if (error != 0) {
        logPrintf("SCD", "stopPeriodicMeasurement() failed (error=%d)", error);
        return false;
    }

    // Periodic measurement mode updates every 5 s; read() polls the
    // data-ready status and only returns once a fresh result is available.
    error = _scd.startPeriodicMeasurement();
    if (error != 0) {
        logPrintf("SCD", "startPeriodicMeasurement() failed (error=%d)", error);
        return false;
    }

    logPrintf("SCD", "initialized at address 0x%02X", SCD4X_I2C_ADDR);
    return true;
}

bool Scd4xSensor::read() {
    // Wait for a fresh measurement (5 s periodic update). Data-ready stays
    // false for ~10 s after startPeriodicMeasurement(), so early wakes just
    // return false until the first result lands.
    bool ready = false;
    int16_t error = _scd.getDataReadyStatus(ready);
    if (error != 0) {
        logPrintf("SCD", "getDataReadyStatus() failed (error=%d)", error);
        return false;
    }
    if (!ready) {
        return false;
    }

    uint16_t co2;
    float temp, hum;
    error = _scd.readMeasurement(co2, temp, hum);
    if (error != 0) {
        logPrintf("SCD", "readMeasurement() failed (error=%d)", error);
        return false;
    }

    _co2 = (int16_t)co2;
    _temp = temp;
    _hum = hum;
    return true;
}

float Scd4xSensor::temperature() const {
    return _temp;
}

float Scd4xSensor::humidity() const {
    return _hum;
}

int16_t Scd4xSensor::co2Concentration() const {
    return _co2;
}

const char* Scd4xSensor::name() const {
    return "SCD4x";
}