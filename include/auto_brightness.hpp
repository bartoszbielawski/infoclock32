#pragma once

// Pure ambient-light -> display-brightness policy (no Arduino / FreeRTOS /
// DataStore), host-tested in tests/host/auto_brightness_check.cpp.
//
//   d     = clamp((lux / lux_max) ^ gamma, 0, 1)    wanted duty fraction
//   level = d * 16 - 0.5                            MAX7219 duty is (2n+1)/32
//
// gamma 0.5 makes the display follow the room's brightness at about the square
// root, which suits the eye's logarithmic response. The raw lux is smoothed in
// the log domain, the applied level only moves when the target is more than
// `hysteresis` levels away, and it slews one level at a time.

#include <algorithm>
#include <cmath>
#include <cstdint>

struct AutoBrightnessParams
{
    float luxMax      = 1000.0f;  // ab_lux_max: lux that maps to full brightness
    float gamma       = 0.5f;     // ab_gamma
    int   minLevel    = 0;        // ab_min
    int   maxLevel    = 15;       // ab_max
    float tauS        = 10.0f;    // ab_tau_s: smoothing time constant
    float hysteresis  = 0.7f;     // levels of dead band around the applied level
    int   slewS       = 1;        // seconds between one-level steps
};

// Make config-supplied values safe.
inline AutoBrightnessParams sanitize(AutoBrightnessParams p)
{
    if (!(p.luxMax > 0.0f))   p.luxMax = 1000.0f;
    if (!(p.gamma >= 0.1f))   p.gamma = 0.1f;
    if (p.gamma > 2.0f)       p.gamma = 2.0f;
    p.minLevel = std::max(0, std::min(15, p.minLevel));
    p.maxLevel = std::max(0, std::min(15, p.maxLevel));
    if (p.maxLevel < p.minLevel) p.maxLevel = p.minLevel;
    if (!(p.tauS >= 0.0f))    p.tauS = 0.0f;
    if (p.slewS < 1)          p.slewS = 1;
    return p;
}

// Continuous (unrounded) target level for a lux value, within min..max.
inline float autoBrightnessTarget(float lux, const AutoBrightnessParams& p)
{
    float d = std::pow(std::max(lux, 0.0f) / p.luxMax, p.gamma);
    d = std::min(1.0f, std::max(0.0f, d));
    float level = d * 16.0f - 0.5f;
    return std::min((float)p.maxLevel, std::max((float)p.minLevel, level));
}

class AutoBrightness
{
public:
    // Forget the smoothed value (call when readings stop being used).
    void reset() { have_ = false; }

    // One control tick. `currentLevel` is what the display shows now. Returns
    // the level to apply (== currentLevel when nothing should change).
    int update(int64_t now, float lux, int currentLevel, const AutoBrightnessParams& raw)
    {
        const AutoBrightnessParams p = sanitize(raw);
        const float lg = std::log(std::max(lux, 0.01f));

        if (!have_)
        {
            smooth_ = lg;
            have_ = true;
        }
        else
        {
            float dt = (float)(now - last_);
            float a = p.tauS > 0.0f ? 1.0f - std::exp(-dt / p.tauS) : 1.0f;
            smooth_ += a * (lg - smooth_);
        }
        last_ = now;

        const float target = autoBrightnessTarget(std::exp(smooth_), p);
        if (std::fabs(target - (float)currentLevel) <= p.hysteresis)
            return currentLevel;
        if (changed_ && (now - lastChange_) < p.slewS)
            return currentLevel;

        changed_ = true;
        lastChange_ = now;
        return target > (float)currentLevel ? currentLevel + 1 : currentLevel - 1;
    }

private:
    bool    have_ = false, changed_ = false;
    float   smooth_ = 0.0f;
    int64_t last_ = 0, lastChange_ = 0;
};
