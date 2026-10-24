// Host-side tests for include/sensor_display.hpp.
// Covers list parsing (order, aliases, unknown/duplicate/empty handling),
// availability gating, formatters and round-trips.
#include <sensor_display.hpp>
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
    expect_list(kSensorDisplayDefault, "temp,humidity,pressure,co2");
    expect_list("auto", "temp,humidity,pressure,co2");
    expect_list("AUTO", "temp,humidity,pressure,co2");
    expect_list("pressure,auto", "pressure,temp,humidity,co2");
    expect_list("auto,pressure", "temp,humidity,pressure,co2");
    expect_list("co2,auto,temp", "co2,temp,humidity,pressure");
    CHECK(displayListHasAuto("auto") && displayListHasAuto("pressure, Auto") && !displayListHasAuto("temp,pressure")
          && !displayListHasAuto(""), "displayListHasAuto");
    expect_list("CO2", "co2");
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
    CHECK(itemAvailable(SDI_TEMP, false, false, false), "temp always available");
    CHECK(!itemAvailable(SDI_HUMIDITY, false, true, false), "humidity needs hasHumidity");
    CHECK(itemAvailable(SDI_HUMIDITY, true, false, false), "humidity ok");
    CHECK(!itemAvailable(SDI_PRESSURE, true, false, false), "pressure needs hasPressure");
    CHECK(itemAvailable(SDI_PRESSURE, false, true, false), "pressure ok");
    CHECK(!itemAvailable(SDI_CO2, true, true, false), "co2 needs hasCo2");
    CHECK(itemAvailable(SDI_CO2, false, false, true), "co2 ok");

    // Temperature formatting (LED degree glyph) — always one fractional digit
    expect_line(formatTempLine(22.5f), std::string("22.5\xF7" "C"), "temp 1 decimal");
    expect_line(formatTempLine(23.4f), std::string("23.4\xF7" "C"), "temp rounds to 1");
    expect_line(formatTempLine(-5.2f), std::string("-5.2\xF7" "C"), "temp negative");
    expect_line(formatTempLine(0.0f), std::string("0.0\xF7" "C"), "temp zero");

    // RuntimeStore format: same value, UTF-8 degree
    {
        char buf[32];
        snprintf(buf, sizeof(buf), runtimeTempFormat(), 22.5f);
        expect_line(buf, std::string("22.5\xC2\xB0" "C"), "runtime temp 1 decimal");
        snprintf(buf, sizeof(buf), runtimeTempFormat(), 22.6f);
        expect_line(buf, std::string("22.6\xC2\xB0" "C"), "runtime temp rounds");
    }

    // Humidity, pressure and CO2
    expect_line(formatHumidityLine(45.4f), "45%RH", "humidity int");
    expect_line(formatHumidityLine(45.6f), "46%RH", "humidity rounds");
    expect_line(formatPressureLine(1013.25f), "1013 hPa", "pressure int");
    expect_line(formatPressureLine(987.6f), "988 hPa", "pressure rounds");
    expect_line(formatCo2Line(412), "412ppmCO2", "co2 int");

    // Round-trip: canonical string parses back to the same items
    {
        std::vector<SensorDisplayItem> items = parseDisplayList("pressure,temp,humidity");
        CHECK(displayListString(items) == "pressure,temp,humidity", "round-trip");
        CHECK(items.size() == 3 && items[0] == SDI_PRESSURE &&
              items[1] == SDI_TEMP && items[2] == SDI_HUMIDITY, "round-trip order");
    }

    // Trend icons: distinct, non-null
    CHECK(trendIconFor(TREND_STEADY) != nullptr, "steady icon");
    CHECK(trendIconFor(TREND_RISING) != trendIconFor(TREND_RISING_FAST), "rising icons differ");
    CHECK(trendIconFor(TREND_FALLING) != trendIconFor(TREND_STEADY), "falling vs steady");

    if (failures == 0)
        printf("all temp display tests passed\n");
    return failures == 0 ? 0 : 1;
}
