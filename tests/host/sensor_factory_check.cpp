// Host-side tests for include/sensor_types.hpp — the pure parseSensorTypes()
// helper (no DataStore / no Arduino / no driver includes, so it links cleanly
// on the host). Covers order, case/whitespace, empty entries, duplicates, and
// the max cap constant.
#include <sensor_types.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static std::string joinList(const std::vector<std::string>& v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i) out += ',';
        out += v[i];
    }
    return out;
}

static void expect_types(const std::string& in, const std::string& want)
{
    std::string got = joinList(parseSensorTypes(in));
    CHECK(got == want, "parse '%s' -> '%s' (want '%s')", in.c_str(), got.c_str(), want.c_str());
}

int main()
{
    // Single type, the shipped default
    expect_types("bme280", "bme280");
    expect_types("stub", "stub");

    // Multiple types, order preserved
    expect_types("bme280,scd4x", "bme280,scd4x");
    expect_types("bmp180,bmp280,sht31,aht20,ds18b20,scd4x",
                 "bmp180,bmp280,sht31,aht20,ds18b20,scd4x");

    // Case + whitespace normalisation
    expect_types("BME280", "bme280");
    expect_types(" bme280 , SCD4X ", "bme280,scd4x");
    expect_types("BME280, scd4x", "bme280,scd4x");

    // Empty entries dropped
    expect_types("bme280,,scd4x,", "bme280,scd4x");
    expect_types(",,", "");
    expect_types("", "");
    expect_types("   ", "");

    // Duplicates collapsed (first position wins)
    expect_types("bme280,bme280", "bme280");
    expect_types("bme280,scd4x,bme280", "bme280,scd4x");
    expect_types("scd4x,bme280,scd4x", "scd4x,bme280");

    // Case-insensitive de-dup
    expect_types("BME280,bme280", "bme280");

    // The list is capped at kMaxSensors by createSensorList(); the parser
    // itself returns everything and the cap is applied by the caller. Verify
    // the constant and that a 5-entry list parses to 5 (cap is the caller's job).
    CHECK(kMaxSensors == 4, "kMaxSensors is 4");
    CHECK(parseSensorTypes("a,b,c,d,e").size() == 5, "parser returns all 5; caller caps");

    if (failures == 0)
        printf("all sensor factory tests passed\n");
    return failures == 0 ? 0 : 1;
}