#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>
#include <temp_sensor.hpp>

// DS18B20 1-Wire temperature sensor.
// Config key: temp_sensor=ds18b20
// Pin config:  ds18b20_pin  (default 4)
//
// Connect data pin to GPIO with a 4.7 kΩ pull-up to 3.3 V.
// Multiple sensors on the same bus are supported; index 0 is always used.
class Ds18b20TempSensor : public TempSensor {
public:
    explicit Ds18b20TempSensor(uint8_t pin)
        : _ow(pin), _dt(&_ow), _temp(0.0f) {}

    bool begin() override {
        _dt.begin();
        uint8_t count = _dt.getDeviceCount();
        if (count == 0) return false;
        _dt.setResolution(12);       // 12-bit, ~750 ms conversion
        _dt.setWaitForConversion(true);
        return true;
    }

    bool read() override {
        _dt.requestTemperatures();
        float t = _dt.getTempCByIndex(0);
        if (t == DEVICE_DISCONNECTED_C) return false;
        _temp = t;
        return true;
    }

    float temperature() const override { return _temp; }
    const char* name() const override { return "DS18B20"; }

private:
    OneWire          _ow;
    DallasTemperature _dt;
    float            _temp;
};
