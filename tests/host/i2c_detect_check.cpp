// Host-side tests for include/i2c_detect.hpp with a fake I2C bus.
#include <i2c_detect.hpp>
#include <cstdio>
#include <map>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

struct FakeBus
{
    std::map<uint8_t, int> dev;   // address -> chip id (-1 = no readable id)
    bool ack(uint8_t a) { return dev.count(a) > 0; }
    int readReg(uint8_t a, uint8_t r) { return (r == 0xD0 && dev.count(a)) ? dev[a] : -1; }
};

static std::string run(FakeBus& b)
{
    std::string out;
    for (const auto& d : detectI2cSensors(b))
    {
        char buf[32];
        snprintf(buf, sizeof buf, "%s@%02x", d.type.c_str(), d.addr);
        out += (out.empty() ? "" : ",") + std::string(buf);
    }
    return out;
}

int main()
{
    { FakeBus b; CHECK(run(b) == "", "empty bus"); }
    {   // the composite board: AHT20 + BMP280 (alt address)
        FakeBus b; b.dev[0x38] = -1; b.dev[0x77] = 0x58;
        CHECK(run(b) == "aht20@38,bmp280@77", "AHT20 + BMP280 at 0x77 (%s)", run(b).c_str());
        FakeBus c; c.dev[0x38] = -1; c.dev[0x76] = 0x58;
        CHECK(run(c) == "aht20@38,bmp280@76", "AHT20 + BMP280 at 0x76 (%s)", run(c).c_str());
    }
    {   // chip IDs
        FakeBus b; b.dev[0x76] = 0x60;
        CHECK(run(b) == "bme280@76", "BME280 by chip id");
        FakeBus c; c.dev[0x77] = 0x56;
        CHECK(run(c) == "bmp280@77", "early BMP280 sample id 0x56");
        FakeBus d; d.dev[0x77] = 0x55;
        CHECK(run(d) == "bmp180@77", "BMP180 at 0x77");
        FakeBus e; e.dev[0x76] = 0x55;
        CHECK(run(e) == "", "0x55 at 0x76 is not a BMP180");
        FakeBus f; f.dev[0x76] = 0x42;
        CHECK(run(f) == "", "unknown chip at 0x76 ignored");
        FakeBus g; g.dev[0x76] = -1;
        CHECK(run(g) == "", "unreadable id ignored");
    }
    {   // fixed-address sensors and the single-per-type rule
        FakeBus b;
        b.dev[0x10] = -1; b.dev[0x62] = -1; b.dev[0x44] = -1; b.dev[0x45] = -1;
        CHECK(run(b) == "sht31@44,scd4x@62,veml7700@10", "sht31 first, 0x44 preferred (%s)", run(b).c_str());
        FakeBus c; c.dev[0x45] = -1;
        CHECK(run(c) == "sht31@45", "SHT31 at the alternate address");
        FakeBus d; d.dev[0x76] = 0x58; d.dev[0x77] = 0x58;
        CHECK(run(d) == "bmp280@76", "two BMP280: only the first kept (%s)", run(d).c_str());
    }
    {   // report order: humidity chips first so they own temperature
        FakeBus b;
        b.dev[0x76] = 0x60; b.dev[0x77] = 0x58; b.dev[0x38] = -1; b.dev[0x44] = -1;
        CHECK(run(b) == "sht31@44,aht20@38,bme280@76,bmp280@77", "order (%s)", run(b).c_str());
    }

    if (failures) { printf("%d i2c detect check(s) FAILED\n", failures); return 1; }
    printf("all i2c detect checks passed\n");
    return 0;
}
