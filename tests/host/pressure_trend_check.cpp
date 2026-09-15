// Host-side tests for include/pressure_trend.hpp.
// Covers classification thresholds, slope estimation, ring rollover,
// staleness and minimum-span rejection.
#include <pressure_trend.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static void expect_kind(float rate, TrendKind want)
{
    CHECK(classifyPressureTrend(rate) == want, "classify %f", (double)rate);
}

int main()
{
    // Classification thresholds (inclusive boundaries)
    expect_kind( 1.0f, TREND_RISING_FAST);
    expect_kind( 0.999f, TREND_RISING);
    expect_kind( 0.2f, TREND_RISING);
    expect_kind( 0.199f, TREND_STEADY);
    expect_kind( 0.0f, TREND_STEADY);
    expect_kind(-0.199f, TREND_STEADY);
    expect_kind(-0.2f, TREND_FALLING);
    expect_kind(-0.999f, TREND_FALLING);
    expect_kind(-1.0f, TREND_FALLING_FAST);

    // Names
    CHECK(pressureTrendName(TREND_RISING_FAST) != nullptr, "name not null");

    // Two samples spanning exactly the minimum: endpoint slope
    {
        PressureRing<8> ring;
        ring.add(1000, 1013.0f);
        ring.add(1000 + 1800, 1011.0f);   // -2 hPa over 1800 s = -4 hPa/h
        float rate = 0;
        CHECK(ring.trend(1000 + 1800, rate), "span ok");
        CHECK(rate < -3.9f && rate > -4.1f, "slope %f == -4", (double)rate);
        CHECK(classifyPressureTrend(rate) == TREND_FALLING_FAST, "-4 hPa/h is falling fast");
    }

    // Span too short → rejected
    {
        PressureRing<8> ring;
        ring.add(1000, 1013.0f);
        ring.add(1000 + 60, 1011.0f);
        float rate = 0;
        CHECK(!ring.trend(1000 + 60, rate), "60 s span rejected");
    }

    // Newest sample too old → rejected
    {
        PressureRing<8> ring;
        ring.add(1000, 1013.0f);
        ring.add(1000 + 1800, 1011.0f);
        float rate = 0;
        CHECK(!ring.trend(1000 + 1800 + kPressureMaxAgeS + 1, rate), "stale sample rejected");
    }

    // Fresh within tolerance
    {
        PressureRing<8> ring;
        ring.add(1000, 1013.0f);
        ring.add(1000 + 1800, 1011.0f);
        float rate = 0;
        CHECK(ring.trend(1000 + 1800 + kPressureMaxAgeS, rate), "sample exactly max age ok");
    }

    // Single sample → rejected
    {
        PressureRing<8> ring;
        ring.add(1000, 1013.0f);
        float rate = 0;
        CHECK(!ring.trend(1000, rate), "one sample rejected");
    }

    // Flat pressure → slope ~0, steady
    {
        PressureRing<16> ring;
        for (int i = 0; i < 10; i++)
            ring.add(1000 + i * 300, 1013.0f);
        float rate = 0;
        CHECK(ring.trend(1000 + 9 * 300, rate), "flat ok");
        CHECK(rate > -0.01f && rate < 0.01f, "flat slope %f ~ 0", (double)rate);
        CHECK(classifyPressureTrend(rate) == TREND_STEADY, "flat is steady");
    }

    // Rising series: +0.5 hPa every 600 s → 3 hPa/h, rising fast
    {
        PressureRing<32> ring;
        for (int i = 0; i < 20; i++)
            ring.add(1000 + i * 600, 1010.0f + i * 0.5f);
        float rate = 0;
        CHECK(ring.trend(1000 + 19 * 600, rate), "rising ok");
        CHECK(rate > 2.8f && rate < 3.2f, "rising slope %f ~ +3", (double)rate);
        CHECK(classifyPressureTrend(rate) == TREND_RISING_FAST, "+3 hPa/h is rising fast");
    }

    // Rollover: far more samples than capacity — slope of the retained tail
    {
        PressureRing<64> ring;
        for (int i = 0; i < 400; i++)
            ring.add(1000 + i * 30, 1013.0f - i * 0.0025f);  // -0.075 hPa per 30 s → -9 hPa/h? no: -0.0025/30 s = -0.3 hPa/h
        float rate = 0;
        CHECK(ring.trend(1000 + 399 * 30, rate), "rollover ok");
        CHECK(rate > -0.45f && rate < -0.15f, "rollover slope %f ~ -0.3", (double)rate);
    }

    // Rollover keeps the span bounded by capacity
    {
        PressureRing<64> ring;
        for (int i = 0; i < 400; i++)
            ring.add(1000 + i * 30, 1013.0f);
        float rate = 0;
        CHECK(ring.trend(1000 + 399 * 30, rate), "rollover flat ok");
        CHECK(classifyPressureTrend(rate) == TREND_STEADY, "flat after rollover");
    }

    // Gentle falling: -0.3 hPa per 600 s → -1.8 hPa/h
    {
        PressureRing<32> ring;
        for (int i = 0; i < 10; i++)
            ring.add(1000 + i * 600, 1020.0f - i * 0.3f);
        float rate = 0;
        CHECK(ring.trend(1000 + 9 * 600, rate), "gentle falling ok");
        CHECK(classifyPressureTrend(rate) == TREND_FALLING_FAST, "-1.8 hPa/h is falling fast");

        PressureRing<32> ring2;
        for (int i = 0; i < 10; i++)
            ring2.add(1000 + i * 600, 1020.0f - i * 0.02f);  // -0.12 hPa/h
        CHECK(ring2.trend(1000 + 9 * 600, rate), "gentle ok");
        CHECK(classifyPressureTrend(rate) == TREND_STEADY, "-0.12 hPa/h is steady");
    }

    if (failures == 0)
        printf("all pressure trend tests passed\n");
    return failures == 0 ? 0 : 1;
}
