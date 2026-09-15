// Host-side tests for include/sun_times.hpp.
// Reference values from api.sunrise-sunset.org (USNO NOAD, refraction -0.833°,
// same convention as compute_sun_times), captured 2026; tolerance ±5 min.
#include <sun_times.hpp>
#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static int hm(int h, int m) { return h * 60 + m; }

static void expect_normal(const char* what, double lat, double lon,
                          int y, int mo, int d, int want_rise, int want_set, int tol)
{
    SunTimes st = compute_sun_times(lat, lon, y, mo, d);
    CHECK(st.kind == SunTimes::Kind::Normal, "%s: expected Normal", what);
    if (st.kind != SunTimes::Kind::Normal) return;
    int drise = st.sunrise_min > want_rise ? st.sunrise_min - want_rise : want_rise - st.sunrise_min;
    int dset  = st.sunset_min  > want_set  ? st.sunset_min  - want_set  : want_set  - st.sunset_min;
    CHECK(drise <= tol, "%s: sunrise %02d:%02d vs %02d:%02d", what,
          st.sunrise_min/60, st.sunrise_min%60, want_rise/60, want_rise%60);
    CHECK(dset <= tol, "%s: sunset %02d:%02d vs %02d:%02d", what,
          st.sunset_min/60, st.sunset_min/60, want_set/60, want_set%60);
}

int main()
{
    // Paris solstice — API 03:44:59Z / 19:59:51Z = 05:44 / 21:59 CEST
    setenv("TZ", "Europe/Paris", 1); tzset();
    expect_normal("Paris 2026-06-21", 48.8566, 2.3522, 2026, 6, 21, hm(5,44), hm(21,59), 5);
    CHECK(hm(5,44) < 720 && 720 < hm(21,59), "sun between noons");

    // Warsaw on the 2026-03-29 DST switch — 04:15:31Z / 17:05:54Z = 06:15 / 19:05 CEST
    setenv("TZ", "Europe/Warsaw", 1); tzset();
    expect_normal("Warsaw 2026-03-29 (DST day)", 52.2297, 21.0122, 2026, 3, 29, hm(6,15), hm(19,5), 5);
    expect_normal("Warsaw 2026-03-28 (CET)", 52.2297, 21.0122, 2026, 3, 28, hm(5,18), hm(18,4), 5);
    // Warsaw 2026-10-25 clock-back day: API 05:17:05Z / 15:22:58Z — events
    // are after the 01:00Z switch to CET(+1) → 06:17 / 16:22 local.
    expect_normal("Warsaw 2026-10-25 (DST day)", 52.2297, 21.0122, 2026, 10, 25, hm(6,17), hm(16,22), 5);

    // Singapore 2026-09-15 — 22:55:20Z(prev day) / 11:04:42Z = 06:55 / 19:04 +08
    setenv("TZ", "Asia/Singapore", 1); tzset();
    expect_normal("Singapore 2026-09-15", 1.3521, 103.8198, 2026, 9, 15, hm(6,55), hm(19,4), 5);

    // Longyearbyen: polar day / polar night (API returns NaN/day_length=0)
    setenv("TZ", "Arctic/Longyearbyen", 1); tzset();
    CHECK(compute_sun_times(78.2232, 15.6267, 2026, 6, 21).kind == SunTimes::Kind::PolarDay,
          "Longyearbyen June = polar day");
    CHECK(compute_sun_times(78.2232, 15.6267, 2026, 1, 5).kind == SunTimes::Kind::PolarNight,
          "Longyearbyen January = polar night");

    // Invariant: Warsaw day length grows toward the solstice.
    setenv("TZ", "Europe/Warsaw", 1); tzset();
    {
        SunTimes mar = compute_sun_times(52.2297, 21.0122, 2026, 3, 20);
        SunTimes jun = compute_sun_times(52.2297, 21.0122, 2026, 6, 21);
        int lenMar = mar.sunset_min - mar.sunrise_min;
        int lenJun = jun.sunset_min - jun.sunrise_min;
        CHECK(lenMar > 700 && lenMar < 760, "equinox day length %d", lenMar);
        CHECK(lenJun > 980, "solstice day length %d", lenJun);
        CHECK(lenJun > lenMar, "grows toward solstice");
    }

    printf(failures ? "%d test(s) FAILED\n" : "all sun tests passed\n", failures);
    return failures != 0;
}
