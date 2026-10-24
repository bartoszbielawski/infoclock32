#pragma once

#include <OneWire.h>
#include <DallasTemperature.h>
#include <sensor.hpp>

// DS18B20 1-Wire temperature sensor.
// Config key: sensors=ds18b20  (comma-separated list; see sensor_factory.hpp)
// Pin config:  ds18b20_pin  (default 4), ds18b20_pullup  (default 0)
//
// Connect data pin to GPIO with a 4.7 kΩ pull-up to 3.3 V.
// Multiple sensors on the same bus are supported; index 0 is always used.
class Ds18b20Sensor : public Sensor {
public:
    explicit Ds18b20Sensor(uint8_t pin, bool pullup = false)
        : _ow(pin), _dt(&_ow), _temp(0.0f), _pin(pin), _pullup(pullup) {}

    bool begin() override {
        // The internal pull-up (~45 kΩ) is much weaker than the 4.7 kΩ the bus
        // wants: OK for a short wire and one sensor, marginal otherwise. Must
        // come after OneWire's constructor, whose pinMode(INPUT) clears it.
        if (_pullup) pinMode(_pin, INPUT_PULLUP);
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
    uint8_t          _pin;
    bool             _pullup;
};
