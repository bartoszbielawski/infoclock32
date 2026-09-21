#pragma once
#ifndef PRESSURE_TREND_HPP
#define PRESSURE_TREND_HPP

#include <time.h>

// Barometric pressure trend: classification and slope estimation from a
// series of (time, hPa) samples. Pure C++ — no Arduino/FreeRTOS — so it can
// be unit tested on the host (see tests/host/pressure_trend_check.cpp).

enum TrendKind
{
    TREND_FALLING_FAST = 0,
    TREND_FALLING,
    TREND_STEADY,
    TREND_RISING,
    TREND_RISING_FAST,
    TREND_KIND_COUNT
};

// Slope thresholds in hPa/hour. Beyond ±1 the change is fast enough to signal
// imminent weather; ±0.2 is a clear but gentle tendency.
static const float kTrendFastHpaPerHour   = 1.0f;
static const float kTrendGentleHpaPerHour = 0.2f;

inline TrendKind classifyPressureTrend(float hpaPerHour)
{
    if (hpaPerHour >=  kTrendFastHpaPerHour)   return TREND_RISING_FAST;
    if (hpaPerHour >=  kTrendGentleHpaPerHour) return TREND_RISING;
    if (hpaPerHour <= -kTrendFastHpaPerHour)   return TREND_FALLING_FAST;
    if (hpaPerHour <= -kTrendGentleHpaPerHour) return TREND_FALLING;
    return TREND_STEADY;
}

inline const char* pressureTrendName(TrendKind kind)
{
    switch (kind)
    {
        case TREND_RISING_FAST:  return "rising fast";
        case TREND_RISING:       return "rising";
        case TREND_FALLING:      return "falling";
        case TREND_FALLING_FAST: return "falling fast";
        default:                 return "steady";
    }
}

// Trend window: the newest sample must be fresher than this, and the stored
// series must span at least kPressureMinSpanS before a slope is trusted.
static const time_t kPressureMaxAgeS    = 900;   // newest sample < 15 min old
static const time_t kPressureMinSpanS   = 1800;  // >= 30 min of history
static const int    kPressureRingCapacity = 480;  // 4 h at the 30 s default interval

// Fixed-capacity ring buffer of pressure samples, oldest dropped when full.
template<int CAP>
class PressureRing
{
public:
    void add(time_t t, float hpa)
    {
        buf_[tail_].t = t;
        buf_[tail_].hpa = hpa;
        tail_ = (tail_ + 1) % CAP;
        if (count_ < CAP) count_++;
    }

    // Least-squares slope over the stored samples, in hPa/hour.
    // Returns true when the newest sample is fresh and the series is long
    // enough for the slope to be meaningful.
    bool trend(time_t now, float& hpaPerHour) const
    {
        if (count_ < 2) return false;

        const int newestIdx = (tail_ + CAP - 1) % CAP;
        const time_t newestT = buf_[newestIdx].t;
        if (now - newestT > kPressureMaxAgeS) return false;

        const int oldestIdx = (tail_ + CAP - count_) % CAP;
        if (newestT - buf_[oldestIdx].t < kPressureMinSpanS) return false;

        // Least squares over relative time (seconds before the newest sample).
        double n = 0, st = 0, sh = 0, stt = 0, sth = 0;
        for (int j = 0; j < count_; j++)
        {
            const int idx = (tail_ + CAP - count_ + j) % CAP;
            const double rel = difftime(newestT, buf_[idx].t);  // >= 0
            const double h = buf_[idx].hpa;
            n++;
            st  += rel;
            sh  += h;
            stt += rel * rel;
            sth += rel * h;
        }
        const double denom = n * stt - st * st;
        if (denom <= 0.0) return false;

        // rel counts backwards in time (seconds before the newest sample),
        // so the physical slope is the negative of the least-squares slope
        // over rel; converted to hPa/hour.
        hpaPerHour = -(n * sth - st * sh) / denom * 3600.0;
        return true;
    }

private:
    struct Sample { time_t t; float hpa; };
    Sample buf_[CAP] = {};
    int tail_  = 0;  // next write position
    int count_ = 0;  // valid samples (up to CAP)
};

#endif // PRESSURE_TREND_HPP
