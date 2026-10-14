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

    // Trigger a new reading. Return true if data is valid.
    virtual bool read() = 0;

    // Always available
    virtual float temperature() const = 0;  // degrees Celsius

    // Optional — override and return true if the sensor provides humidity
    virtual bool hasHumidity() const { return false; }
    virtual float humidity() const { return 0.0f; }     // %RH

    // Optional — override and return true if the sensor provides pressure
    virtual bool hasPressure() const { return false; }
    virtual float pressure() const { return 0.0f; }     // hPa

    // Optional — override and return true if the sensor provides CO2
    virtual bool hasCo2() const { return false; }
    virtual int16_t co2Concentration() const { return 0; }   // ppm (0 = unavailable)

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
