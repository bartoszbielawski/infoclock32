// Host-side tests for include/weather_icons.hpp.
// Verifies the OpenWeatherMap condition-id mapping and renders every icon
// as ASCII art (stdout) for eyeballing the bitmaps.
#include <weather_icons.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static void expect_icon(int id, bool night, int want, const char* what)
{
    CHECK(weatherIconIndex(id, night) == want, "%s: id %d night %d", what, id, (int)night);
}

int main()
{
    // Full OWM id space — every id must land on a sane icon.
    for (int id = 200; id < 300; id++)
        expect_icon(id, false, WI_THUNDER, "2xx");
    for (int id = 300; id < 400; id++)
        expect_icon(id, false, WI_DRIZZLE, "3xx");
    for (int id = 500; id < 600; id++)
        expect_icon(id, false, id == 511 ? WI_SNOW : WI_RAIN, "5xx");
    for (int id = 600; id < 700; id++)
        expect_icon(id, false, WI_SNOW, "6xx");
    for (int id = 700; id < 800; id++)
        expect_icon(id, false, WI_FOG, "7xx");
    expect_icon(800, false, WI_SUN, "clear day");
    expect_icon(800, true, WI_MOON, "clear night");
    for (int id = 801; id <= 802; id++)
        expect_icon(id, false, WI_PARTLY, "partly");
    for (int id = 803; id <= 804; id++)
        expect_icon(id, false, WI_CLOUD, "overcast");

    // Out-of-range / unparseable ids default to cloud.
    expect_icon(0, false, WI_CLOUD, "0");
    expect_icon(-1, false, WI_CLOUD, "-1");
    expect_icon(999, false, WI_CLOUD, "999");
    expect_icon(805, false, WI_CLOUD, "805");
    expect_icon(511, true, WI_SNOW, "511 night still snow");

    // Render each icon as ASCII for visual inspection.
    for (int i = 0; i < WI_COUNT; i++)
    {
        printf("icon %d (%dx8):\n", i, kWeatherIconWidth);
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < kWeatherIconWidth; x++)
                putchar((kWeatherIcons[i][x] >> y) & 1 ? '#' : '.');
            putchar('\n');
        }
        putchar('\n');
    }

    if (failures == 0)
        printf("all icon checks passed\n");
    return failures == 0 ? 0 : 1;
}
