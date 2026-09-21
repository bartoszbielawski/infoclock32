#pragma once

#include <Adafruit_BMP085.h>
#include <temp_sensor.hpp>

// BMP180 implementation of TempSensor (I2C, fixed address 0x77).
class Bmp180TempSensor : public TempSensor {
public:
    bool begin() override { return _bmp.begin(); }

    bool read() override {
        _temp     = _bmp.readTemperature();
        _pressure = _bmp.readPressure() / 100.0f;  // Pa → hPa
        return !isnan(_temp) && !isnan(_pressure);
    }

    float temperature() const override { return _temp; }

    bool  hasPressure() const override { return true; }
    float pressure()    const override { return _pressure; }

    const char* name() const override { return "BMP180"; }

private:
    Adafruit_BMP085 _bmp;
    float _temp     = 0.0f;
    float _pressure = 0.0f;
};
