// Host-side tests for include/weather_wmo.hpp (Open-Meteo WMO codes).
#include <weather_wmo.hpp>
#include <weather_icons.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static void expect(int wmo, int owm, int icon, const char* en)
{
    CHECK(wmo_to_owm(wmo) == owm, "wmo %d -> owm %d, want %d", wmo, wmo_to_owm(wmo), owm);
    CHECK(weatherIconIndex(wmo_to_owm(wmo)) == icon, "wmo %d icon %d, want %d",
          wmo, weatherIconIndex(wmo_to_owm(wmo)), icon);
    CHECK(strcmp(wmo_description(wmo, Lang::En), en) == 0, "wmo %d text '%s', want '%s'",
          wmo, wmo_description(wmo, Lang::En), en);
}

int main()
{
    expect(0,  800, WI_SUN,     "clear sky");
    expect(1,  801, WI_PARTLY,  "mainly clear");
    expect(2,  802, WI_PARTLY,  "partly cloudy");
    expect(3,  804, WI_CLOUD,   "overcast");
    expect(45, 741, WI_FOG,     "fog");
    expect(48, 741, WI_FOG,     "fog");
    expect(51, 300, WI_DRIZZLE, "drizzle");
    expect(55, 302, WI_DRIZZLE, "drizzle");
    expect(56, 511, WI_SNOW,    "freezing drizzle");
    expect(61, 500, WI_RAIN,    "rain");
    expect(65, 502, WI_RAIN,    "rain");
    expect(67, 511, WI_SNOW,    "freezing rain");
    expect(71, 600, WI_SNOW,    "snow");
    expect(77, 600, WI_SNOW,    "snow");
    expect(80, 520, WI_RAIN,    "rain showers");
    expect(82, 522, WI_RAIN,    "rain showers");
    expect(85, 620, WI_SNOW,    "snow showers");
    expect(95, 211, WI_THUNDER, "thunderstorm");
    expect(99, 202, WI_THUNDER, "thunderstorm");
    // unknown codes fall back to overcast
    expect(-1, 804, WI_CLOUD,   "overcast");
    expect(42, 804, WI_CLOUD,   "overcast");
    CHECK(weatherIconIndex(wmo_to_owm(0), true) == WI_MOON, "clear at night is the moon");

    // every language has a non-empty ASCII text for every code
    const Lang langs[] = {Lang::En, Lang::Fr, Lang::Pl, Lang::It, Lang::Es, Lang::Pt, Lang::De};
    for (Lang l : langs)
        for (int wmo = -1; wmo <= 100; wmo++) {
            const char* t = wmo_description(wmo, l);
            CHECK(t && *t, "empty text lang %d wmo %d", (int)l, wmo);
            for (const char* p = t; p && *p; p++)
                CHECK((unsigned char)*p < 128, "non-ASCII in '%s'", t);
        }
    CHECK(strcmp(wmo_description(63, Lang::Pl), "deszcz") == 0, "pl rain");
    CHECK(strcmp(wmo_description(95, Lang::De), "Gewitter") == 0, "de thunder");

    if (failures) { printf("%d weather wmo check(s) FAILED\n", failures); return 1; }
    printf("all weather wmo checks passed\n");
    return 0;
}
