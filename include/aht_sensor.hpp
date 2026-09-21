#pragma once

#include <Adafruit_AHTX0.h>
#include <temp_sensor.hpp>

// AHT10 / AHT20 I2C temperature + humidity sensor (Aosong).
// Config key: temp_sensor=aht10  or  temp_sensor=aht20
// Both variants share I2C address 0x38 (fixed, not configurable).
//
// AHT20 adds a factory-calibrated humidity element and is generally preferred.
// The Adafruit AHTX0 library handles both transparently.
// Wire.begin() must be called before begin().
class AhtTempSensor : public TempSensor {
public:
    explicit AhtTempSensor(bool isAht20)
        : _name(isAht20 ? "AHT20" : "AHT10"), _temp(0.0f), _hum(0.0f) {}

    bool begin() override {
        return _aht.begin();
    }

    bool read() override {
        sensors_event_t tEv, hEv;
        if (!_aht.getEvent(&hEv, &tEv)) return false;
        float t = tEv.temperature;
        float h = hEv.relative_humidity;
        if (isnan(t) || isnan(h)) return false;
        _temp = t;
        _hum  = h;
        return true;
    }

    float temperature()  const override { return _temp; }
    bool  hasHumidity()  const override { return true;  }
    float humidity()     const override { return _hum;  }
    const char* name()   const override { return _name; }

private:
    Adafruit_AHTX0 _aht;
    const char*    _name;
    float          _temp;
    float          _hum;
};
