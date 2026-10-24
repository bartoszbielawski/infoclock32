#pragma once

// Builds the list of Sensor implementations from the `sensors` config key.
// Must be called after DataStore::load_from_file() and Wire.begin() (for I2C
// sensors).
//
// `sensors` is a comma-separated list of driver types (same syntax as
// `sensor_display`). Examples:
//   sensors=bme280             single sensor
//   sensors=bme280,scd4x       two sensors, polled round-robin
//   sensors=auto               scan the I2C bus for known sensors (the default; see i2c_detect.hpp)
//   sensors=stub               disabled
//   sensors=auto,ds18b20       detected I2C sensors plus a 1-Wire one (not detectable)
//
//   type      | Sensor class  | Extra config keys
//   ----------|---------------|-----------------------------------
//   bmp180    | Bmp180Sensor  | — (address 0x77, fixed)
//   bmp280    | Bmp280Sensor  | bmp280_addr  (default 0x76)
//   bme280    | Bme280Sensor  | bme280_addr  (default 0x76)
//   sht31     | Sht31Sensor   | sht31_addr   (default 0x44)
//   aht10     | AhtSensor     | — (address 0x38, fixed)
//   aht20     | AhtSensor     | — (address 0x38, fixed)
//   ds18b20   | Ds18b20Sensor | ds18b20_pin  (default 4), ds18b20_pullup (default 0)
//   scd4x     | Scd4xSensor   | — (address 0x62, fixed; adds CO2)
//   veml7700  | Veml7700Sensor| — (address 0x10, fixed; lux only, feeds auto-brightness)
//   stub      | StubSensor    | — (no hardware)
//   sim[N]    | SimSensor     | sim_temp, sim_humidity, ... (virtual, see sim_sensor.hpp)
//   <other>   | (skipped)     | unknown types are warned about and dropped
//
// Duplicates are removed (first occurrence wins) and the list is capped at
// kMaxSensors — extras are warned about and ignored. Two of the same type on
// one bus would collide on address anyway, so de-duping by type is safe.
//
// DS18B20 does NOT use I2C — Wire.begin() is still safe to call but has no
// effect on it.

#include <string>
#include <vector>
#include <sensor.hpp>
#include <sensor_types.hpp>
#include <i2c_detect.hpp>
#include <i2c_scan.hpp>
#include <runtime_store.hpp>
#include <Wire.h>
#include <map>
#include <bmp180_sensor.hpp>
#include <bmp280_sensor.hpp>
#include <bme280_sensor.hpp>
#include <sht31_sensor.hpp>
#include <aht_sensor.hpp>
#include <ds18b20_sensor.hpp>
#include <scd4x_sensor.hpp>
#include <veml7700_sensor.hpp>
#include <sim_sensor.hpp>
#include <data_store.hpp>
#include <logger.hpp>

// Address for an I2C sensor: the autodetected one if given, else the config
// key, else the chip's default.
template <class Store>
inline uint8_t i2cAddr(Store& ds, const char* key, int def, int override)
{
    return (uint8_t)(override >= 0 ? override : ds.template get_value<int>(key, def));
}

// Instantiate one sensor for a known type, reading its address/pin from the
// matching config key. Returns nullptr for unknown types (the caller decides
// whether to warn and skip).
inline Sensor* createSensorForType(const std::string& type, int addrOverride = -1)
{
    auto& ds = DataStore::getInstance();

    if (type == "bmp180")
        return new Bmp180Sensor();
    if (type == "bmp280")
        return new Bmp280Sensor(i2cAddr(ds, "bmp280_addr", 0x76, addrOverride));
    if (type == "bme280")
        return new Bme280Sensor(i2cAddr(ds, "bme280_addr", 0x76, addrOverride));
    if (type == "sht31")
        return new Sht31Sensor(i2cAddr(ds, "sht31_addr", 0x44, addrOverride));
    if (type == "aht10")
        return new AhtSensor(/*isAht20=*/false);
    if (type == "aht20")
        return new AhtSensor(/*isAht20=*/true);
    if (type == "ds18b20")
        return new Ds18b20Sensor((uint8_t)ds.get_value<int>("ds18b20_pin", 4),
                                 ds.get_value<int>("ds18b20_pullup", 0) != 0);
    if (type == "scd4x")
        return new Scd4xSensor();
    if (type == "veml7700")
        return new Veml7700Sensor();
    if (type == "stub")
        return new StubSensor();
    if (isSimType(type))   // sim, sim2, ... : virtual sensor, see sim_sensor.hpp
        return new SimSensor(simParamsFromStore(ds, type), "Sim");
    return nullptr;
}

// Wire-backed bus for detectI2cSensors().
struct WireBus
{
    bool ack(uint8_t addr)
    {
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    }
    int readReg(uint8_t addr, uint8_t reg)
    {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return -1;
        if (Wire.requestFrom((int)addr, 1) != 1) return -1;
        return Wire.read();
    }
};

// Build the sensor list from the `sensors` config key. Unknown types are
// logged and skipped; the list is capped at kMaxSensors. Returns an empty
// vector when `sensors` is `auto` and nothing was found on the bus. `stub` is
// a valid (no-hardware) entry — a single "stub" returns one StubSensor, which
// disables the sensor task.
// Scan the whole bus and log every device that answers (boot diagnostics; also
// finds chips we have no driver for). Stores the addresses in the runtime value
// `i2c_devices`. Call after Wire.begin().
inline void logI2cScan()
{
    WireBus bus;
    std::vector<uint8_t> found = scanI2c(bus);
    for (const auto& line : formatScanLines(found))
        logPrintf("I2C", "%s", line.c_str());
    RuntimeStore::getInstance().set("i2c_devices", scanSummary(found));
}

inline std::vector<Sensor*> createSensorList()
{
    std::string list = DataStore::getInstance().get_value("sensors", "auto");
    std::vector<std::string> types = parseSensorTypes(list);

    // `auto`: scan the bus once and use the addresses it found.
    std::map<std::string, int> foundAddr;
    if (listHasAuto(types))
    {
        WireBus bus;
        std::vector<DetectedSensor> found = detectI2cSensors(bus);
        std::vector<std::string> foundTypes;
        for (const auto& d : found)
        {
            logPrintf("TMP", "autodetect: %s at 0x%02X", d.type.c_str(), d.addr);
            foundTypes.push_back(d.type);
            foundAddr[d.type] = d.addr;
        }
        if (found.empty())
            logPrintf("TMP", "autodetect: no known I2C sensor found");
        types = expandAutoTypes(types, foundTypes);
    }

    std::vector<Sensor*> out;
    for (const auto& type : types)
    {
        if (out.size() >= kMaxSensors)
        {
            logPrintf("TMP", "sensor '%s' exceeds max of %d — skipped",
                      type.c_str(), kMaxSensors);
            continue;
        }
        auto fa = foundAddr.find(type);
        Sensor* s = createSensorForType(type, fa != foundAddr.end() ? fa->second : -1);
        if (s == nullptr)
        {
            logPrintf("TMP", "unknown sensor type '%s' — skipped", type.c_str());
            continue;
        }
        out.push_back(s);
    }
    return out;
}