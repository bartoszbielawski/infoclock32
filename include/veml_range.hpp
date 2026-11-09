#pragma once

// Non-blocking range control for the VEML7700 light sensor (pure, host-tested
// in tests/host/veml_range_check.cpp).
//
// The Adafruit library's VEML_LUX_AUTO restarts from the least sensitive
// setting on every read and waits two integration times per step: about 3.5 s
// per reading in a dark room, every 2 s poll. Instead the sensor keeps its
// position on this ladder (the same steps the library's auto-ranging walks)
// and moves at most one step per reading, so each read is a single register
// access. The library's thresholds and lux formula are kept.

#include <cstdint>

struct VemlSetting
{
    uint8_t gainCode;   // VEML7700_GAIN_* register value
    float   gain;
    uint8_t itCode;     // VEML7700_IT_* register value
    int     itMs;
};

// Least to most sensitive.
inline constexpr VemlSetting kVemlLadder[] = {
    {0x02, 0.125f, 0x0C,  25},   // gain 1/8, 25 ms   (very bright)
    {0x02, 0.125f, 0x08,  50},   // gain 1/8, 50 ms
    {0x02, 0.125f, 0x00, 100},   // gain 1/8, 100 ms  (library's starting point)
    {0x03, 0.25f,  0x00, 100},   // gain 1/4
    {0x00, 1.0f,   0x00, 100},   // gain 1
    {0x01, 2.0f,   0x00, 100},   // gain 2
    {0x01, 2.0f,   0x01, 200},
    {0x01, 2.0f,   0x02, 400},
    {0x01, 2.0f,   0x03, 800},   // gain 2, 800 ms    (darkest)
};
inline constexpr int kVemlSteps      = sizeof(kVemlLadder) / sizeof(kVemlLadder[0]);
inline constexpr int kVemlStartIndex = 2;

// Raw counts outside this window lose resolution or saturate (library values).
inline constexpr uint16_t kVemlRawLow  = 100;
inline constexpr uint16_t kVemlRawHigh = 10000;

// Ladder position for the gain/IT the sensor is set to; start index if the
// combination isn't on the ladder.
inline int vemlIndexFor(uint8_t gainCode, uint8_t itCode)
{
    for (int i = 0; i < kVemlSteps; i++)
        if (kVemlLadder[i].gainCode == gainCode && kVemlLadder[i].itCode == itCode)
            return i;
    return kVemlStartIndex;
}

// Where to go after reading `raw` at `index`: one step at a time.
inline int vemlNextIndex(int index, uint16_t raw)
{
    if (raw <= kVemlRawLow && index < kVemlSteps - 1) return index + 1;
    if (raw > kVemlRawHigh && index > 0)              return index - 1;
    return index;
}

// The library's lux formula (Adafruit_VEML7700::computeLux): resolution
// 0.0036 lx/count at gain 2 and 800 ms, scaled by gain and integration time.
// The non-linear correction applies at gain 1/8, i.e. in bright light, which is
// where the library's auto-ranging applies it as well.
inline float vemlLux(int index, uint16_t raw)
{
    const VemlSetting& s = kVemlLadder[index];
    float lux = 0.0036f * (800.0f / (float)s.itMs) * (2.0f / s.gain) * (float)raw;
    if (index <= kVemlStartIndex)
        lux = (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
    return lux;
}
