// Host-side tests for include/auto_brightness.hpp: the lux -> level curve,
// smoothing, hysteresis and slewing.
#include <auto_brightness.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static int level_for(float lux, const AutoBrightnessParams& p)
{
    return (int)std::lround(autoBrightnessTarget(lux, p));
}

// Settle the controller at constant lux; returns the final level.
static int settle(AutoBrightness& c, int64_t& t, float lux, int level, const AutoBrightnessParams& p, int secs)
{
    for (int i = 0; i < secs; i++)
        level = c.update(t++, lux, level, p);
    return level;
}

int main()
{
    const AutoBrightnessParams p;   // defaults: 1000 lx max, gamma 0.5, 0..15

    // ── the curve (documented table) ──────────────────────────────────────
    CHECK(level_for(0.0f, p) == 0,     "0 lx -> 0 (%d)", level_for(0.0f, p));
    CHECK(level_for(1.0f, p) == 0,     "1 lx -> 0");
    CHECK(level_for(10.0f, p) == 1,    "10 lx -> 1 (%d)", level_for(10.0f, p));
    CHECK(level_for(60.0f, p) == 3,    "60 lx -> 3 (%d)", level_for(60.0f, p));
    CHECK(level_for(220.0f, p) == 7,   "220 lx -> 7 (%d)", level_for(220.0f, p));
    CHECK(level_for(1000.0f, p) == 15, "1000 lx -> 15");
    CHECK(level_for(100000.0f, p) == 15, "bright sun clamps to 15");
    CHECK(level_for(-5.0f, p) == 0,    "negative lux treated as dark");
    {   // monotonic
        int prev = 0; bool mono = true;
        for (float l = 0.1f; l < 3000.0f; l *= 1.2f)
        {
            int v = level_for(l, p);
            mono = mono && v >= prev; prev = v;
        }
        CHECK(mono, "curve is monotonic");
    }
    {   // min/max clamp
        AutoBrightnessParams q; q.minLevel = 2; q.maxLevel = 10;
        CHECK(level_for(0.0f, q) == 2 && level_for(5000.0f, q) == 10, "ab_min/ab_max clamp");
    }

    // ── sanitize ──────────────────────────────────────────────────────────
    {
        AutoBrightnessParams q; q.luxMax = -1; q.gamma = 99; q.minLevel = 12; q.maxLevel = 3; q.slewS = 0; q.tauS = -4;
        q = sanitize(q);
        CHECK(q.luxMax == 1000.0f && q.gamma == 2.0f, "luxMax/gamma sanitized");
        CHECK(q.maxLevel == q.minLevel && q.minLevel == 12, "max raised to min");
        CHECK(q.slewS == 1 && q.tauS == 0.0f, "slew/tau sanitized");
    }

    // ── control behaviour ─────────────────────────────────────────────────
    {   // converges to the target and stops
        AutoBrightness c; int64_t t = 0;
        int lv = settle(c, t, 220.0f, 3, p, 30);
        CHECK(lv == 7, "settles at 7 for 220 lx (%d)", lv);
        int again = settle(c, t, 220.0f, lv, p, 30);
        CHECK(again == 7, "stays put");
    }
    {   // slews one level per second
        AutoBrightness c; int64_t t = 0;
        int lv = 0;
        lv = c.update(t++, 1000.0f, lv, p);
        CHECK(lv == 1, "first step is one level (%d)", lv);
        lv = c.update(t++, 1000.0f, lv, p);
        CHECK(lv == 2, "next second another level (%d)", lv);
        int same = c.update(t - 1, 1000.0f, lv, p);   // same second again
        CHECK(same == lv, "no second step within the slew interval");
    }
    {   // a short flash of light (hand wave) barely moves the level
        AutoBrightness c; int64_t t = 0;
        int lv = settle(c, t, 20.0f, 2, p, 60);
        int base = lv;
        lv = c.update(t++, 800.0f, lv, p);   // one bright sample
        lv = settle(c, t, 20.0f, lv, p, 20);
        CHECK(lv <= base + 1, "one bright sample is smoothed away (%d vs %d)", lv, base);
        CHECK(lv == base, "and returns to the base level (%d vs %d)", lv, base);
    }
    {   // hysteresis: lux jittering around a boundary does not flip the level
        AutoBrightness c; int64_t t = 0;
        int lv = settle(c, t, 250.0f, 7, p, 60);   // target 7.5: between 7 and 8
        int changes = 0;
        for (int i = 0; i < 200; i++)
        {
            int nl = c.update(t++, (i & 1) ? 240.0f : 260.0f, lv, p);
            if (nl != lv) changes++;
            lv = nl;
        }
        CHECK(changes == 0, "no flicker near a boundary (%d changes)", changes);
    }
    {   // falls back down when the room goes dark
        AutoBrightness c; int64_t t = 0;
        int lv = settle(c, t, 1000.0f, 15, p, 30);
        lv = settle(c, t, 0.5f, lv, p, 120);
        CHECK(lv == 0, "dark room reaches level 0 (%d)", lv);
    }
    {   // reset forgets the smoothed value
        AutoBrightness c; int64_t t = 0;
        settle(c, t, 1000.0f, 15, p, 30);
        c.reset();
        int lv = c.update(t++, 0.5f, 15, p);
        CHECK(lv == 14, "after reset the new sample counts fully (%d)", lv);
    }

    if (failures) { printf("%d auto brightness check(s) FAILED\n", failures); return 1; }
    printf("all auto brightness checks passed\n");
    return 0;
}
