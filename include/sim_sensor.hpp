#pragma once

// Virtual sensor for development and tests: no hardware, deterministic
// values, scripted failures. Pure C++ (no Arduino), so it runs in the host
// tests and in the host prototype as well as on a device.
//
// Config: put `sim` (or `sim2`, `sim3`, ... for several) in the `sensors` list.
// Keys are prefixed with the type name, e.g. for `sim`:
//
//   sim_temp=21.5          base temperature, degC            (default 21)
//   sim_humidity=45        base %RH; presence enables humidity
//   sim_pressure=1013      base hPa; presence enables pressure
//   sim_co2=600            base ppm; presence enables CO2
//   sim_lux=200            base lux; presence enables ambient light
//   sim_no_temp=1          no temperature (a light-only sensor)
//   sim_poll_s=2           own poll period in seconds (0 = round-robin)
//   sim_drift=1.5          +/- amplitude of a slow sine added to every value
//   sim_drift_period=60    reads per drift cycle              (default 60)
//   sim_fail_every=5       every 5th read attempt fails (intermittent)
//   sim_fail_after=10      after 10 good reads the sensor "dies" ...
//   sim_fail_count=20      ... for 20 attempts, then recovers (-1 = forever)
//   sim_begin_fail=1       begin() reports failure (missing chip)
//
// Example: sensors=sim,sim2  with sim2_co2=800 and sim_fail_after=3 shows the
// second sensor taking over temperature when the first dies.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <cctype>
#include <sensor.hpp>

struct SimParams
{
    float temp = 21.0f;
    bool  hasHumidity = false; float humidity = 45.0f;
    bool  hasPressure = false; float pressure = 1013.0f;
    bool  hasCo2 = false;      int   co2 = 600;
    bool  hasLux = false;      float lux = 200.0f;
    bool  hasTemp = true;
    int   pollS = 0;
    float drift = 0.0f;
    int   driftPeriod = 60;
    int   failEvery = 0;      // 0 = never
    int   failAfter = -1;     // -1 = never dies
    int   failCount = -1;     // attempts spent dead; -1 = forever
    bool  beginFails = false;
};

// True for "sim", "sim2", "sim17" ...
inline bool isSimType(const std::string& type)
{
    if (type.size() < 3 || type.compare(0, 3, "sim") != 0) return false;
    for (size_t i = 3; i < type.size(); i++)
        if (!isdigit((unsigned char)type[i])) return false;
    return true;
}

// Build params from any store exposing has_key(std::string) and
// get_value<T>(std::string, T) (DataStore does). `type` is the sensors entry.
template <class Store>
SimParams simParamsFromStore(Store& ds, const std::string& type)
{
    const std::string p = type + "_";
    SimParams sp;
    sp.temp        = ds.template get_value<float>(p + "temp", sp.temp);
    sp.hasHumidity = ds.has_key(p + "humidity");
    if (sp.hasHumidity) sp.humidity = ds.template get_value<float>(p + "humidity", sp.humidity);
    sp.hasPressure = ds.has_key(p + "pressure");
    if (sp.hasPressure) sp.pressure = ds.template get_value<float>(p + "pressure", sp.pressure);
    sp.hasCo2      = ds.has_key(p + "co2");
    if (sp.hasCo2) sp.co2 = ds.template get_value<int>(p + "co2", sp.co2);
    sp.hasLux      = ds.has_key(p + "lux");
    if (sp.hasLux) sp.lux = ds.template get_value<float>(p + "lux", sp.lux);
    sp.hasTemp     = ds.template get_value<int>(p + "no_temp", 0) == 0;
    sp.pollS       = ds.template get_value<int>(p + "poll_s", 0);
    sp.drift       = ds.template get_value<float>(p + "drift", 0.0f);
    sp.driftPeriod = ds.template get_value<int>(p + "drift_period", sp.driftPeriod);
    if (sp.driftPeriod < 2) sp.driftPeriod = 2;
    sp.failEvery   = ds.template get_value<int>(p + "fail_every", 0);
    sp.failAfter   = ds.template get_value<int>(p + "fail_after", -1);
    sp.failCount   = ds.template get_value<int>(p + "fail_count", -1);
    sp.beginFails  = ds.template get_value<int>(p + "begin_fail", 0) != 0;
    return sp;
}

class SimSensor : public Sensor
{
public:
    explicit SimSensor(const SimParams& p = SimParams(), const char* label = "Sim")
        : p_(p), label_(label) {}

    bool begin() override { return !p_.beginFails; }

    bool read() override
    {
        attempts_++;
        if (p_.failEvery > 0 && attempts_ % p_.failEvery == 0)
            return false;

        if (p_.failAfter >= 0 && good_ >= p_.failAfter &&
            (p_.failCount < 0 || deadAttempts_ < p_.failCount))
        {
            deadAttempts_++;
            return false;
        }

        double phase = 2.0 * 3.14159265358979 * (double)(good_ % p_.driftPeriod) / p_.driftPeriod;
        float wobble = p_.drift * (float)std::sin(phase);
        t_  = p_.temp + wobble;
        rh_ = p_.humidity + wobble;
        hp_ = p_.pressure + wobble;
        co2_ = p_.co2 + (int)(wobble * 10.0f);
        lux_ = p_.lux * (1.0f + 0.1f * wobble);
        good_++;
        return true;
    }

    float temperature() const override { return t_; }
    bool  hasHumidity() const override { return p_.hasHumidity; }
    float humidity() const override { return rh_; }
    bool  hasPressure() const override { return p_.hasPressure; }
    float pressure() const override { return hp_; }
    bool  hasCo2() const override { return p_.hasCo2; }
    int16_t co2Concentration() const override { return (int16_t)co2_; }
    bool  hasTemperature() const override { return p_.hasTemp; }
    bool  hasLux() const override { return p_.hasLux; }
    float lux() const override { return lux_; }
    int   pollIntervalS() const override { return p_.pollS; }
    const char* name() const override { return label_; }

    // Inspection for tests
    int attempts() const { return attempts_; }
    int goodReads() const { return good_; }

private:
    SimParams p_;
    const char* label_;
    int attempts_ = 0, good_ = 0, deadAttempts_ = 0;
    float t_ = 0, rh_ = 0, hp_ = 0;
    int co2_ = 0;
    float lux_ = 0;
};
