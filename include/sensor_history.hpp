#pragma once

// 24 h of sensor readings for the web dashboard chart: one sample per
// `history_step_min` bucket (default 5 min -> 288 samples, ~3.4 KB of static
// RAM, no heap). Pure ring logic, host-tested in
// tests/host/sensor_history_check.cpp; the device-side singleton with its
// lock is SensorHistoryStore (sensor_history_store.hpp).

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>

enum HistoryFlag : uint8_t {
    HF_TEMP = 1, HF_RH = 2, HF_HPA = 4, HF_CO2 = 8,
};

struct HistorySample {
    uint32_t t = 0;        // wall time (s) of the bucket's first reading
    int16_t  temp10 = 0;   // deg C x10
    uint16_t hpa10 = 0;    // hPa x10
    uint16_t co2 = 0;      // ppm
    uint8_t  rh = 0;       // %
    uint8_t  flags = 0;    // HistoryFlag bits: which fields are valid
};

// Quantise readings into a sample; NaN / out-of-range values are left out.
inline HistorySample make_history_sample(uint32_t t, float temp, float rh, float hpa, int co2)
{
    HistorySample s;
    s.t = t;
    if (!isnan(temp) && temp > -100.0f && temp < 150.0f) {
        s.temp10 = (int16_t)lroundf(temp * 10.0f); s.flags |= HF_TEMP;
    }
    if (!isnan(rh) && rh >= 0.0f && rh <= 100.0f) {
        s.rh = (uint8_t)lroundf(rh); s.flags |= HF_RH;
    }
    if (!isnan(hpa) && hpa > 100.0f && hpa < 2000.0f) {
        s.hpa10 = (uint16_t)lroundf(hpa * 10.0f); s.flags |= HF_HPA;
    }
    if (co2 > 0 && co2 < 65535) {
        s.co2 = (uint16_t)co2; s.flags |= HF_CO2;
    }
    return s;
}

class SensorHistory {
public:
    static constexpr size_t kCapacity = 288;

    // Store `s` if it opens a new bucket of `stepS` seconds; a later reading in
    // the same bucket or a sample with nothing valid is ignored. Returns true
    // when stored.
    //
    // The newest sample's bucket is worked out with the current `stepS`, so a
    // history_step_min change at run time takes effect on the next reading
    // (a remembered bucket index from the old step would block every sample
    // after a step increase). A clock stepped back by more than one bucket
    // (NTP correcting a bad time) starts the history afresh: the stored
    // samples would otherwise sit in the "future" and block new ones.
    bool offer(const HistorySample& s, uint32_t stepS)
    {
        if (stepS == 0 || s.flags == 0) return false;
        if (count_) {
            const uint32_t lastT = at(count_ - 1).t;
            if (s.t + stepS < lastT)
                clear();
            else if (s.t / stepS <= lastT / stepS)
                return false;
        }
        buf_[head_] = s;
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) count_++;
        return true;
    }

    size_t size() const { return count_; }

    // i = 0 is the oldest sample.
    const HistorySample& at(size_t i) const
    {
        return buf_[(head_ + kCapacity - count_ + i) % kCapacity];
    }

    void clear() { head_ = count_ = 0; }

private:
    HistorySample buf_[kCapacity];
    size_t head_ = 0, count_ = 0;
};

// Format one series value for JSON (`null` when the sample lacks it).
// field: 0 temp, 1 rh, 2 hpa, 3 co2. Returns the characters written.
inline int history_value_json(const HistorySample& s, int field, char* out, size_t n)
{
    switch (field) {
        case 0: return (s.flags & HF_TEMP) ? snprintf(out, n, "%.1f", s.temp10 / 10.0) : snprintf(out, n, "null");
        case 1: return (s.flags & HF_RH)   ? snprintf(out, n, "%u", (unsigned)s.rh)    : snprintf(out, n, "null");
        case 2: return (s.flags & HF_HPA)  ? snprintf(out, n, "%.1f", s.hpa10 / 10.0)  : snprintf(out, n, "null");
        default: return (s.flags & HF_CO2) ? snprintf(out, n, "%u", (unsigned)s.co2)   : snprintf(out, n, "null");
    }
}
