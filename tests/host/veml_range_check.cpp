// Host-side tests for include/veml_range.hpp.
#include <veml_range.hpp>
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

// The library's own formula, written out independently.
static float libraryLux(float gain, int itMs, uint16_t raw, bool corrected)
{
    float lux = 0.0036f * (800.0f / itMs) * (2.0f / gain) * raw;
    if (corrected)
        lux = (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
    return lux;
}

// Simulate the sensor: raw counts for a true lux at a ladder step (linear,
// clipped at the 16-bit register).
static uint16_t rawFor(float lux, int idx)
{
    const VemlSetting& s = kVemlLadder[idx];
    float res = 0.0036f * (800.0f / s.itMs) * (2.0f / s.gain);
    float r = lux / res;
    return r > 65535.0f ? 65535 : (uint16_t)(r + 0.5f);
}

int main()
{
    // ladder sanity: strictly more sensitive at each step
    for (int i = 1; i < kVemlSteps; i++)
    {
        float prev = kVemlLadder[i - 1].gain * kVemlLadder[i - 1].itMs;
        float cur  = kVemlLadder[i].gain * kVemlLadder[i].itMs;
        CHECK(cur > prev, "step %d not more sensitive than %d", i, i - 1);
    }
    CHECK(kVemlLadder[kVemlStartIndex].gain == 0.125f && kVemlLadder[kVemlStartIndex].itMs == 100,
          "start = library's gain 1/8, 100 ms");

    // index lookup
    CHECK(vemlIndexFor(0x01, 0x03) == kVemlSteps - 1, "gain 2 / 800 ms is the darkest step");
    CHECK(vemlIndexFor(0x02, 0x0C) == 0, "gain 1/8 / 25 ms is the brightest step");
    CHECK(vemlIndexFor(0x00, 0x03) == kVemlStartIndex, "off-ladder combination -> start");

    // one step at a time, clamped at both ends
    CHECK(vemlNextIndex(4, 50) == 5, "dark -> more sensitive");
    CHECK(vemlNextIndex(4, 20000) == 3, "bright -> less sensitive");
    CHECK(vemlNextIndex(4, 5000) == 4, "in range -> stay");
    CHECK(vemlNextIndex(kVemlSteps - 1, 0) == kVemlSteps - 1, "darkest stays");
    CHECK(vemlNextIndex(0, 65535) == 0, "brightest stays");

    // lux matches the library formula at every step
    for (int i = 0; i < kVemlSteps; i++)
    {
        const VemlSetting& s = kVemlLadder[i];
        float want = libraryLux(s.gain, s.itMs, 5000, i <= kVemlStartIndex);
        float got = vemlLux(i, 5000);
        CHECK(std::fabs(got - want) <= want * 1e-5f, "step %d: %f vs %f", i, got, want);
    }

    // convergence: from the start, a dark room and a sunny window settle in
    // range within a few readings, and the reported lux is then accurate
    for (float trueLux : {0.5f, 5.0f, 80.0f, 400.0f, 3000.0f})
    {
        int idx = kVemlStartIndex, steps = 0;
        for (int n = 0; n < 20; n++)
        {
            int next = vemlNextIndex(idx, rawFor(trueLux, idx));
            if (next == idx) break;
            idx = next; steps++;
        }
        uint16_t raw = rawFor(trueLux, idx);
        float got = vemlLux(idx, raw);
        bool linear = idx > kVemlStartIndex;
        CHECK(steps <= kVemlSteps, "%.1f lx: converged in %d steps", trueLux, steps);
        if (linear)
            CHECK(std::fabs(got - trueLux) <= trueLux * 0.05f + 0.01f,
                  "%.1f lx reads %.2f at step %d", trueLux, got, idx);
    }
    // darkness ends at the most sensitive step
    {
        int idx = kVemlStartIndex;
        for (int n = 0; n < 20; n++) idx = vemlNextIndex(idx, 0);
        CHECK(idx == kVemlSteps - 1, "0 lx walks to the darkest step (%d)", idx);
    }

    if (failures) { printf("%d VEML range check(s) FAILED\n", failures); return 1; }
    printf("all VEML range checks passed\n");
    return 0;
}
