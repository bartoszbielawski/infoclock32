#pragma once

#include <Adafruit_SHT31.h>
#include <temp_sensor.hpp>

// SHT30 / SHT31 / SHT35 I2C temperature + humidity sensor (Sensirion).
// Config key: temp_sensor=sht31
// Address:    sht31_addr  (default 0x44; 0x45 if ADDR pin pulled high)
//
// Accuracy: ±0.3 °C, ±2 %RH.  No pull-ups needed on I2C lines (use 4.7 kΩ
// or the board's own pull-ups).  Wire.begin() must be called before begin().
class Sht31TempSensor : public TempSensor {
public:
    explicit Sht31TempSensor(uint8_t addr = 0x44)
        : _addr(addr), _temp(0.0f), _hum(0.0f) {}

    bool begin() override {
        return _sht.begin(_addr);
    }

    bool read() override {
        float t = _sht.readTemperature();
        float h = _sht.readHumidity();
        if (isnan(t) || isnan(h)) return false;
        _temp = t;
        _hum  = h;
        return true;
    }

    float temperature()  const override { return _temp; }
    bool  hasHumidity()  const override { return true;  }
    float humidity()     const override { return _hum;  }
    const char* name()   const override { return "SHT31"; }

private:
    Adafruit_SHT31 _sht;
    uint8_t        _addr;
    float          _temp;
    float          _hum;
};
