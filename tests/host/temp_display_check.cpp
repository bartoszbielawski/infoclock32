// Host-side tests for include/temp_display.hpp.
// Covers list parsing (order, aliases, unknown/duplicate/empty handling),
// availability gating, formatters and round-trips.
#include <temp_display.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static void expect_list(const std::string& in, const std::string& want)
{
    std::string got = displayListString(parseDisplayList(in));
    CHECK(got == want, "parse '%s' -> '%s' (want '%s')", in.c_str(), got.c_str(), want.c_str());
}

static void expect_line(const std::string& got, const std::string& want, const char* what)
{
    CHECK(got == want, "%s: got '%s' want '%s'", what, got.c_str(), want.c_str());
}

int main()
{
    // List parsing: default, order, aliases, case/whitespace
    expect_list(kTempDisplayDefault, "temp,pressure");
    expect_list("", "");
    expect_list("   ", "");
    expect_list("TEMP", "temp");
    expect_list(" humidity ", "humidity");
    expect_list("temperature, RH , HPA", "temp,humidity,pressure");
    expect_list("temp,pressure", "temp,pressure");

    // Unknown tokens dropped, duplicates collapsed (first position wins)
    expect_list("bogus", "");
    expect_list("temp,bogus,pressure", "temp,pressure");
    expect_list("temp,pressure,temp", "temp,pressure");
    expect_list("pressure,temp,pressure", "pressure,temp");

    // Empty entries between commas
    expect_list("temp,,pressure,", "temp,pressure");

    // Availability gating
    CHECK(itemAvailable(TDI_TEMP, false, false), "temp always available");
    CHECK(!itemAvailable(TDI_HUMIDITY, false, true), "humidity needs hasHumidity");
    CHECK(itemAvailable(TDI_HUMIDITY, true, false), "humidity ok");
    CHECK(!itemAvailable(TDI_PRESSURE, true, false), "pressure needs hasPressure");
    CHECK(itemAvailable(TDI_PRESSURE, false, true), "pressure ok");

    // Temperature formatting (LED degree glyph)
    expect_line(formatTempLine(22.5f, 1), std::string("22.5\xF7" "C"), "temp 1 decimal");
    expect_line(formatTempLine(23.4f, 0), std::string("23\xF7" "C"), "temp 0 decimals");
    expect_line(formatTempLine(-5.2f, 1), std::string("-5.2\xF7" "C"), "temp negative");
    expect_line(formatTempLine(0.0f, 0), std::string("0\xF7" "C"), "temp zero 0 decimals");

    // RuntimeStore format: same value, UTF-8 degree
    {
        char buf[32];
        snprintf(buf, sizeof(buf), runtimeTempFormat(1), 22.5f);
        expect_line(buf, std::string("22.5\xC2\xB0" "C"), "runtime temp 1 decimal");
        snprintf(buf, sizeof(buf), runtimeTempFormat(0), 22.6f);
        expect_line(buf, std::string("23\xC2\xB0" "C"), "runtime temp 0 decimals");
    }

    // Humidity and pressure
    expect_line(formatHumidityLine(45.4f), "45%RH", "humidity int");
    expect_line(formatHumidityLine(45.6f), "46%RH", "humidity rounds");
    expect_line(formatPressureLine(1013.25f), "1013 hPa", "pressure int");
    expect_line(formatPressureLine(987.6f), "988 hPa", "pressure rounds");

    // Round-trip: canonical string parses back to the same items
    {
        std::vector<TempDisplayItem> items = parseDisplayList("pressure,temp,humidity");
        CHECK(displayListString(items) == "pressure,temp,humidity", "round-trip");
        CHECK(items.size() == 3 && items[0] == TDI_PRESSURE &&
              items[1] == TDI_TEMP && items[2] == TDI_HUMIDITY, "round-trip order");
    }

    // Trend icons: distinct, non-null
    CHECK(trendIconFor(TREND_STEADY) != nullptr, "steady icon");
    CHECK(trendIconFor(TREND_RISING) != trendIconFor(TREND_RISING_FAST), "rising icons differ");
    CHECK(trendIconFor(TREND_FALLING) != trendIconFor(TREND_STEADY), "falling vs steady");

    if (failures == 0)
        printf("all temp display tests passed\n");
    return failures == 0 ? 0 : 1;
}
