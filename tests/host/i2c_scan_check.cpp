// Host-side tests for include/i2c_scan.hpp (boot-time bus scan formatting) and
// the accuracy ordering of include/i2c_detect.hpp.
#include <i2c_scan.hpp>
#include <i2c_detect.hpp>
#include <cstdio>
#include <set>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

struct FakeBus
{
    std::set<uint8_t> dev;
    int probed = 0;
    uint8_t lo = 255, hi = 0;
    bool ack(uint8_t a) { probed++; lo = a < lo ? a : lo; hi = a > hi ? a : hi; return dev.count(a) > 0; }
    int readReg(uint8_t, uint8_t) { return -1; }
};

int main()
{
    // ── scan ──────────────────────────────────────────────────────────────
    {
        FakeBus b;
        CHECK(scanI2c(b).empty(), "empty bus");
        CHECK(b.lo == 0x08 && b.hi == 0x77 && b.probed == 0x77 - 0x08 + 1,
              "probes the usable range only (0x%02X..0x%02X, %d probes)", b.lo, b.hi, b.probed);
        auto lines = formatScanLines({});
        CHECK(lines.size() == 1 && lines[0] == "i2c scan: no devices answered", "empty bus line");
    }
    {
        FakeBus b; b.dev = {0x68, 0x3C, 0x38, 0x76, 0x7A};   // 0x7A is outside the range
        auto found = scanI2c(b);
        CHECK(found.size() == 4 && found[0] == 0x38 && found[3] == 0x76, "found in address order (%zu)", found.size());
        CHECK(scanSummary(found) == "0x38,0x3C,0x68,0x76", "summary '%s'", scanSummary(found).c_str());
        auto lines = formatScanLines(found);
        CHECK(lines.size() == 5 && lines[0] == "i2c scan: 4 device(s)", "header line");
        CHECK(lines[1] == "  0x38 AHT10/AHT20/FT6206", "known address described (%s)", lines[1].c_str());
        CHECK(lines[2].find("SSD1306") != std::string::npos, "OLED recognised");
        CHECK(lines[3].find("DS3231") != std::string::npos, "RTC recognised (other ICs)");
    }
    CHECK(describeI2c(0x7A) == "0x7A", "unknown address has no description");
    CHECK(describeI2c(0x21).find("PCF8574") != std::string::npos, "range-matched expander");

    // ── accuracy ordering ─────────────────────────────────────────────────
    // The sensor task gives each measurement to the first listed provider, so
    // the best-first order by temperature must also be best-first for humidity
    // and pressure. Checked over the whole table, for every pair.
    {
        std::vector<DetectedSensor> all;
        for (const auto& a : kAccuracy) all.push_back({a.type, 0});
        sortByAccuracy(all);

        auto better_or_equal = [](float first, float later) { return first <= later; };
        for (size_t i = 0; i < all.size(); i++)
            for (size_t j = i + 1; j < all.size(); j++)
            {
                const SensorAccuracy* x = accuracyFor(all[i].type);
                const SensorAccuracy* y = accuracyFor(all[j].type);
                if (x->rh > 0 && y->rh > 0)
                    CHECK(better_or_equal(x->rh, y->rh), "humidity: %s before %s", x->type, y->type);
                if (x->hpa > 0 && y->hpa > 0)
                    CHECK(better_or_equal(x->hpa, y->hpa), "pressure: %s before %s", x->type, y->type);
                if (x->tempC > 0 && y->tempC > 0)
                    CHECK(better_or_equal(x->tempC, y->tempC), "temperature: %s before %s", x->type, y->type);
            }
        CHECK(all.back().type == std::string("veml7700"), "light sensor sorts last (%s)", all.back().type.c_str());
        CHECK(all.front().type == std::string("sht31"), "SHT31 is the most accurate (%s)", all.front().type.c_str());
        auto pos = [&](const char* t) { for (size_t i = 0; i < all.size(); i++) if (all[i].type == t) return (int)i; return -1; };
        CHECK(pos("scd4x") > pos("bmp180"), "SCD4x (self-heating) ranks below the pressure chips for temperature");
    }
    {   // unknown types sort after everything; stable for equal accuracy
        std::vector<DetectedSensor> v = {{"mystery", 1}, {"bmp180", 2}, {"bmp280", 3}, {"aht20", 4}};
        sortByAccuracy(v);
        CHECK(v[0].type == "aht20" && v[1].type == "bmp180" && v[2].type == "bmp280" && v[3].type == "mystery",
              "equal accuracy keeps input order, unknown last (%s %s %s %s)",
              v[0].type.c_str(), v[1].type.c_str(), v[2].type.c_str(), v[3].type.c_str());
    }

    if (failures) { printf("%d i2c scan check(s) FAILED\n", failures); return 1; }
    printf("all i2c scan checks passed\n");
    return 0;
}
