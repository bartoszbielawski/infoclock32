#pragma once

#include <Adafruit_BME280.h>
#include <temp_sensor.hpp>

// BME280 I2C temperature + humidity + pressure sensor (Bosch).
// Config key: temp_sensor=bme280
// Address:    bme280_addr  (default 0x76; 0x77 if SDO pulled high)
//
// Drop-in upgrade from BMP280 — same pinout, adds humidity measurement.
// Wire.begin() must be called before begin().
class Bme280TempSensor : public TempSensor {
public:
    explicit Bme280TempSensor(uint8_t addr = 0x76)
        : _addr(addr), _temp(0.0f), _pressure(0.0f), _hum(0.0f) {}

    bool begin() override {
        return _bme.begin(_addr);
    }

    bool read() override {
        float t = _bme.readTemperature();
        float p = _bme.readPressure() / 100.0f;  // Pa → hPa
        float h = _bme.readHumidity();
        if (isnan(t) || isnan(p) || isnan(h)) return false;
        _temp     = t;
        _pressure = p;
        _hum      = h;
        return true;
    }

    float temperature()  const override { return _temp;     }
    bool  hasPressure()  const override { return true;      }
    float pressure()     const override { return _pressure; }
    bool  hasHumidity()  const override { return true;      }
    float humidity()     const override { return _hum;      }
    const char* name()   const override { return "BME280";  }

private:
    Adafruit_BME280 _bme;
    uint8_t         _addr;
    float           _temp;
    float           _pressure;
    float           _hum;
};
