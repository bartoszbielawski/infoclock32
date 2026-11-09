#pragma once

#include <cstdint>
#include <utility>

// Abstract interface for a sensor that provides temperature, humidity,
// pressure, and optionally CO2 concentration.
class Sensor {
public:
    virtual ~Sensor() = default;

    // Initialize the sensor. Return true on success.
    virtual bool begin() = 0;

    // Optional first half of a two-phase measurement: start a conversion and
    // return the milliseconds until read() can collect it, so the sensor task
    // can start every due sensor at once and collect them together instead of
    // waiting on each in turn.
    //   0   (default) read() measures by itself
    //   > 0 collect with read() after this many ms
    //   < 0 the start failed; counted as a failed read
    // read() must still work on its own (it then measures and waits itself).
    virtual int startMeasurement() { return 0; }

    // Take (or, after startMeasurement(), collect) a reading. Return true if
    // the data is valid.
    virtual bool read() = 0;

    // Degrees Celsius. Meaningful only when hasTemperature() is true.
    virtual float temperature() const = 0;

    // Optional — override and return false for sensors that do not measure
    // temperature (e.g. ambient light sensors), so they never own `temp_c`.
    virtual bool hasTemperature() const { return true; }

    // Optional — override and return true if the sensor provides humidity
    virtual bool hasHumidity() const { return false; }
    virtual float humidity() const { return 0.0f; }     // %RH

    // Optional — override and return true if the sensor provides pressure
    virtual bool hasPressure() const { return false; }
    virtual float pressure() const { return 0.0f; }     // hPa

    // Optional — override and return true if the sensor provides CO2
    virtual bool hasCo2() const { return false; }
    virtual int16_t co2Concentration() const { return 0; }   // ppm (0 = unavailable)

    // Optional — override and return true if the sensor provides ambient light
    virtual bool hasLux() const { return false; }
    virtual float lux() const { return 0.0f; }   // lx

    // Own poll period in seconds. 0 = polled round-robin at `sensor_interval`.
    // A positive value takes the sensor out of the round: it is polled on its
    // own timer (used for ambient light, which must track quickly).
    virtual int pollIntervalS() const { return 0; }

    // Short sensor name for log messages
    virtual const char* name() const = 0;

    // True only for the no-hardware placeholder; the sensor task drops stubs
    // from the polling round (and exits when nothing else is configured).
    virtual bool isStub() const { return false; }
};

// Stub implementation — no hardware, read() always returns false.
class StubSensor : public Sensor {
public:
    bool begin() override { return true; }
    bool read() override { return false; }
    float temperature() const override { return 0.0f; }
    int16_t co2Concentration() const override { return 0; }
    const char* name() const override { return "Stub"; }
    bool isStub() const override { return true; }
};
