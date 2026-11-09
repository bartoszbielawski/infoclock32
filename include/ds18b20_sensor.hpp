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
//
// Two-phase: startMeasurement() starts the ~750 ms 12-bit conversion and
// returns at once; the sensor task collects it with read() on a later pass
// instead of blocking for the conversion. read() alone still works (it then
// starts the conversion and waits, as before).
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
        _dt.setWaitForConversion(false);   // conversions run while we do other work
        return true;
    }

    int startMeasurement() override {
        // requestTemperatures() doesn't report a missing device; a bus reset
        // without a presence pulse does.
        if (!_ow.reset()) { _started = false; return -1; }
        _dt.requestTemperatures();
        _started = true;
        return (int)_dt.millisToWaitForConversion();
    }

    bool read() override {
        if (!_started) {
            // Called without startMeasurement(): measure and wait here.
            int ms = startMeasurement();
            if (ms < 0) return false;
            delay((uint32_t)ms);
        }
        _started = false;
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
    bool             _started = false;   // a conversion is waiting to be read
};
