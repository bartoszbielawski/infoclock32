#pragma once

#include <Adafruit_BMP280.h>
#include <temp_sensor.hpp>

// BMP280 implementation of TempSensor (I2C).
// Default I2C address is 0x76; pass 0x77 if SDO is pulled high.
class Bmp280TempSensor : public TempSensor {
public:
    explicit Bmp280TempSensor(uint8_t addr = BMP280_ADDRESS_ALT)
        : _addr(addr), _temp(0.0f), _pressure(0.0f) {}

    bool begin() override {
        return _bmp.begin(_addr);
    }

    bool read() override {
        _temp     = _bmp.readTemperature();   // °C
        _pressure = _bmp.readPressure() / 100.0f;  // Pa → hPa
        return !isnan(_temp) && !isnan(_pressure);
    }

    float temperature() const override { return _temp; }

    bool  hasPressure()    const override { return true; }
    float pressure()       const override { return _pressure; }

    const char* name() const override { return "BMP280"; }

private:
    Adafruit_BMP280 _bmp;
    uint8_t  _addr;
    float    _temp;
    float    _pressure;
};
