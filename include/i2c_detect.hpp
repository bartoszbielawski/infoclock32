#pragma once

// I2C sensor autodetection for `sensors=auto` — pure (no Arduino / Wire), so
// the decision logic is host-tested in tests/host/i2c_detect_check.cpp.
//
// `Bus` supplies two operations (see WireBus in sensor_factory.hpp):
//   bool ack(uint8_t addr)                 true if a device answers at addr
//   int  readReg(uint8_t addr, uint8_t r)  one register byte, -1 on failure
//
// Addresses are fixed per chip family, apart from the BMP280/BME280/BMP180
// group which shares 0x76/0x77 and is told apart by its chip-ID register.
//
//   0x44/0x45  sht31      ack only
//   0x38       aht20      ack only (AHT10 shares the address and cannot be
//                         told apart; list `aht10` explicitly if you have one)
//   0x76/0x77  bmp280 / bme280 / bmp180   chip ID at 0xD0
//   0x62       scd4x      ack only
//   0x10       veml7700   ack only
//
// A wrong guess is harmless: a sensor whose begin() fails is dropped from the
// round. At most one sensor per type is returned.
//
// Order = accuracy. The sensor task gives each measurement to the first listed
// sensor that provides it, so the result is sorted best-first by kAccuracy
// (temperature accuracy is the sort key; every sensor but the light sensor
// measures it). kAccuracy is checked in the host test so that the same order is
// also best-first for humidity and pressure.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

struct DetectedSensor
{
    std::string type;   // as in the `sensors` list
    uint8_t     addr;   // where it answered
};

enum I2cChipId { kBmp180Id = 0x55, kBmp280IdFirst = 0x56, kBmp280IdLast = 0x58, kBme280Id = 0x60 };

// "bmp280" / "bme280" / "bmp180" for a chip-ID byte, "" when unknown.
inline const char* bmeFamilyFromChipId(int id, uint8_t addr)
{
    if (id >= kBmp280IdFirst && id <= kBmp280IdLast) return "bmp280";
    if (id == kBme280Id)                             return "bme280";
    if (id == kBmp180Id && addr == 0x77)             return "bmp180";   // BMP180 is fixed at 0x77
    return "";
}

// Typical datasheet accuracy, +/- (0 = does not measure it). Approximate: only
// the ranking matters. SCD4x temperature is rated +/-0.8 degC but reads high
// from self-heating, so it is ranked as a poor temperature source.
struct SensorAccuracy
{
    const char* type;
    float tempC;    // degC
    float rh;       // %RH
    float hpa;      // hPa
};

inline constexpr SensorAccuracy kAccuracy[] = {
    // type        temp   rh    hPa
    { "sht31",     0.2f,  2.0f, 0.0f },
    { "aht20",     0.3f,  2.0f, 0.0f },
    { "bme280",    0.5f,  3.0f, 1.0f },
    { "bmp280",    1.0f,  0.0f, 1.0f },
    { "bmp180",    1.0f,  0.0f, 1.5f },
    { "scd4x",     3.0f,  6.0f, 0.0f },   // temp: self-heating, see above
    { "veml7700",  0.0f,  0.0f, 0.0f },   // light only
};

inline const SensorAccuracy* accuracyFor(const std::string& type)
{
    for (const auto& a : kAccuracy)
        if (type == a.type) return &a;
    return nullptr;
}

// Sort key: temperature accuracy, with sensors that have none (light) last and
// unknown types after everything.
inline float temperatureRank(const std::string& type)
{
    const SensorAccuracy* a = accuracyFor(type);
    if (!a) return 1000.0f;
    return a->tempC > 0.0f ? a->tempC : 999.0f;
}

// Best-first, stable (equal accuracy keeps detection order).
inline void sortByAccuracy(std::vector<DetectedSensor>& v)
{
    std::stable_sort(v.begin(), v.end(), [](const DetectedSensor& x, const DetectedSensor& y) {
        return temperatureRank(x.type) < temperatureRank(y.type);
    });
}

template <class Bus>
std::vector<DetectedSensor> detectI2cSensors(Bus& bus)
{
    std::vector<DetectedSensor> out;
    auto add = [&](const char* type, uint8_t addr)
    {
        for (const auto& d : out)
            if (d.type == type) return;   // one per type
        out.push_back({type, addr});
    };

    // Probe order does not matter: the result is sorted by accuracy below.
    for (uint8_t a : {0x44, 0x45})
        if (bus.ack(a)) { add("sht31", a); break; }

    if (bus.ack(0x38)) add("aht20", 0x38);

    // Pressure family: collect first, then emit bme280 before bmp280/bmp180.
    uint8_t bmeAddr = 0, bmpAddr = 0, bmp180Addr = 0;
    for (uint8_t a : {0x76, 0x77})
    {
        if (!bus.ack(a)) continue;
        std::string fam = bmeFamilyFromChipId(bus.readReg(a, 0xD0), a);
        if (fam == "bme280" && !bmeAddr)      bmeAddr = a;
        else if (fam == "bmp280" && !bmpAddr) bmpAddr = a;
        else if (fam == "bmp180" && !bmp180Addr) bmp180Addr = a;
    }
    if (bmeAddr)    add("bme280", bmeAddr);
    if (bmpAddr)    add("bmp280", bmpAddr);
    if (bmp180Addr) add("bmp180", bmp180Addr);

    if (bus.ack(0x62)) add("scd4x", 0x62);
    if (bus.ack(0x10)) add("veml7700", 0x10);

    sortByAccuracy(out);
    return out;
}
