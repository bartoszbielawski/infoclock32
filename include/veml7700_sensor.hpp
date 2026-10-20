#pragma once

#include <Adafruit_VEML7700.h>
#include <cmath>
#include <sensor.hpp>

// VEML7700 ambient light sensor (Vishay), I2C address 0x10 (fixed).
// Config key: sensors=veml7700  (comma-separated list; see sensor_factory.hpp)
//
// Provides lux only (no temperature). Gain and integration time are auto-ranged
// by the library and the non-linear response at high light is corrected, so the
// reading is usable from ~0.01 lx to >100 klx. Wire.begin() must be called
// before begin().
//
// Polled every 2 s on its own timer (pollIntervalS) so auto-brightness can
// follow the room; see include/auto_brightness.hpp. Mount it so the LED matrix
// does not shine on it, otherwise brightness feeds back into the reading.
class Veml7700Sensor : public Sensor {
public:
    bool begin() override { return _veml.begin(); }

    bool read() override {
        float l = _veml.readLux(VEML_LUX_AUTO);
        if (std::isnan(l) || l < 0.0f) return false;
        _lux = l;
        return true;
    }

    float temperature() const override { return 0.0f; }
    bool  hasTemperature() const override { return false; }
    bool  hasLux() const override { return true; }
    float lux() const override { return _lux; }
    int   pollIntervalS() const override { return 2; }
    const char* name() const override { return "VEML7700"; }

private:
    Adafruit_VEML7700 _veml;
    float             _lux = 0.0f;
};
