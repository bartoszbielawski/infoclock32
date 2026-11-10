#pragma once

// Linear calibration T = slope * raw + offset (pure, host-tested in
// tests/host/linear_cal_check.cpp). Used per sensor (DS18B20).
//
// From two reference measurements (sensor read raw1 at true ref1, raw2 at
// true ref2):
//   slope  = (ref2 - ref1) / (raw2 - raw1)
//   offset = ref1 - slope * raw1
// A plain offset correction is slope = 1.

#include <cmath>

struct LinearCal
{
    float slope  = 1.0f;
    float offset = 0.0f;
    bool  active() const { return slope != 1.0f || offset != 0.0f; }
};

// A real sensor's gain error is a few percent at most: outside this range
// the value is a typo, and applying it would make readings nonsense.
constexpr float kMinCalSlope = 0.5f;
constexpr float kMaxCalSlope = 1.5f;

// Validate configured values (NAN = not set -> the default). `why` (optional)
// explains a rejection for the log; a rejected calibration is the identity.
inline LinearCal makeLinearCal(float slope, float offset, const char** why = nullptr)
{
    LinearCal c;
    if (why) *why = "";
    if (std::isnan(slope))  slope = 1.0f;
    if (std::isnan(offset)) offset = 0.0f;
    if (!(slope >= kMinCalSlope && slope <= kMaxCalSlope))
    {
        if (why) *why = "slope outside 0.5-1.5";
        return c;
    }
    if (!(std::fabs(offset) <= 20.0f))
    {
        if (why) *why = "offset beyond +/-20 degC";
        return c;
    }
    c.slope = slope;
    c.offset = offset;
    return c;
}

inline float applyLinearCal(const LinearCal& c, float raw) { return c.slope * raw + c.offset; }

// Coefficients from two reference points (as documented above); false when
// the points are too close (< 1 degC) to give a reliable slope.
inline bool linearCalFromTwoPoints(float raw1, float ref1, float raw2, float ref2, LinearCal& out)
{
    if (std::fabs(raw2 - raw1) < 1.0f) return false;
    out.slope  = (ref2 - ref1) / (raw2 - raw1);
    out.offset = ref1 - out.slope * raw1;
    return true;
}
