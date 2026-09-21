#pragma once

// Abstract interface for a local temperature (and optionally humidity/pressure) sensor.
// Implement this class for your specific hardware, then pass it to temp_sensor_task.
class TempSensor {
public:
    virtual ~TempSensor() = default;

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

    // Short sensor name for log messages
    virtual const char* name() const = 0;
};

// Stub implementation — no hardware, read() always returns false.
// Replace new StubTempSensor() in main.cpp with your concrete sensor class.
class StubTempSensor : public TempSensor {
public:
    bool begin() override { return true; }
    bool read() override { return false; }
    float temperature() const override { return 0.0f; }
    const char* name() const override { return "Stub"; }
};
