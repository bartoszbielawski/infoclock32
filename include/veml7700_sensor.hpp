#pragma once

#include <Adafruit_VEML7700.h>
#include <cmath>
#include <sensor.hpp>
#include <veml_range.hpp>

// VEML7700 ambient light sensor (Vishay), I2C address 0x10 (fixed).
// Config key: sensors=veml7700  (comma-separated list; see sensor_factory.hpp)
//
// Provides lux only (no temperature). Usable from ~0.01 lx to >100 klx: gain
// and integration time follow the light one step per reading (veml_range.hpp),
// so a read never blocks; only begin() does one blocking auto-range to find
// the starting point. Wire.begin() must be called before begin().
//
// Polled every 2 s on its own timer (pollIntervalS) so auto-brightness can
// follow the room; see include/auto_brightness.hpp. 2 s also covers the
// longest integration (800 ms) twice. Mount it so the LED matrix does not
// shine on it, otherwise brightness feeds back into the reading.

static_assert(VEML7700_GAIN_1_8 == 0x02 && VEML7700_GAIN_1_4 == 0x03 &&
              VEML7700_GAIN_1 == 0x00 && VEML7700_GAIN_2 == 0x01,
              "veml_range.hpp gain codes must match the library");
static_assert(VEML7700_IT_25MS == 0x0C && VEML7700_IT_50MS == 0x08 &&
              VEML7700_IT_100MS == 0x00 && VEML7700_IT_200MS == 0x01 &&
              VEML7700_IT_400MS == 0x02 && VEML7700_IT_800MS == 0x03,
              "veml_range.hpp integration-time codes must match the library");

class Veml7700Sensor : public Sensor {
public:
    bool begin() override {
        if (!_veml.begin()) return false;
        // One blocking auto-range at boot leaves the sensor on a sensible
        // setting for the current light; from then on reads don't wait.
        float l = _veml.readLux(VEML_LUX_AUTO);
        _index = vemlIndexFor(_veml.getGain(), _veml.getIntegrationTime());
        if (!std::isnan(l) && l >= 0.0f) { _lux = l; _have = true; }
        return true;
    }
    bool read() override {
        uint16_t raw = _veml.readALS(false);   // last completed integration
        if (_settling) {
            // First sample after a range change may mix both settings:
            // keep reporting the previous value once.
            _settling = false;
            return _have;
        }
        _lux = vemlLux(_index, raw);
        _have = true;
        int next = vemlNextIndex(_index, raw);
        if (next != _index) {
            _index = next;
            _veml.setGain(kVemlLadder[_index].gainCode);
            _veml.setIntegrationTime(kVemlLadder[_index].itCode, /*wait=*/false);
            _settling = true;
        }
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
    bool              _have = false;
    bool              _settling = false;
    int               _index = kVemlStartIndex;
};
