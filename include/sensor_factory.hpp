#pragma once

// Builds the list of Sensor implementations from the `sensors` config key.
// Must be called after DataStore::load_from_file() and Wire.begin() (for I2C
// sensors).
//
// `sensors` is a comma-separated list of driver types (same syntax as
// `sensor_display`). Examples:
//   sensors=bme280             single sensor
//   sensors=bme280,scd4x       two sensors, polled round-robin
//   sensors=stub               disabled (the default)
//
//   type      | Sensor class  | Extra config keys
//   ----------|---------------|-----------------------------------
//   bmp180    | Bmp180Sensor  | — (address 0x77, fixed)
//   bmp280    | Bmp280Sensor  | bmp280_addr  (default 0x76)
//   bme280    | Bme280Sensor  | bme280_addr  (default 0x76)
//   sht31     | Sht31Sensor   | sht31_addr   (default 0x44)
//   aht10     | AhtSensor     | — (address 0x38, fixed)
//   aht20     | AhtSensor     | — (address 0x38, fixed)
//   ds18b20   | Ds18b20Sensor | ds18b20_pin  (default 4)
//   scd4x     | Scd4xSensor   | — (address 0x62, fixed; adds CO2)
//   stub      | StubSensor    | — (no hardware)
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
#include <bmp180_sensor.hpp>
#include <bmp280_sensor.hpp>
#include <bme280_sensor.hpp>
#include <sht31_sensor.hpp>
#include <aht_sensor.hpp>
#include <ds18b20_sensor.hpp>
#include <scd4x_sensor.hpp>
#include <data_store.hpp>
#include <logger.hpp>

// Instantiate one sensor for a known type, reading its address/pin from the
// matching config key. Returns nullptr for unknown types (the caller decides
// whether to warn and skip).
inline Sensor* createSensorForType(const std::string& type)
{
    auto& ds = DataStore::getInstance();

    if (type == "bmp180")
        return new Bmp180Sensor();
    if (type == "bmp280")
        return new Bmp280Sensor((uint8_t)ds.get_value<int>("bmp280_addr", 0x76));
    if (type == "bme280")
        return new Bme280Sensor((uint8_t)ds.get_value<int>("bme280_addr", 0x76));
    if (type == "sht31")
        return new Sht31Sensor((uint8_t)ds.get_value<int>("sht31_addr", 0x44));
    if (type == "aht10")
        return new AhtSensor(/*isAht20=*/false);
    if (type == "aht20")
        return new AhtSensor(/*isAht20=*/true);
    if (type == "ds18b20")
        return new Ds18b20Sensor((uint8_t)ds.get_value<int>("ds18b20_pin", 4));
    if (type == "scd4x")
        return new Scd4xSensor();
    if (type == "stub")
        return new StubSensor();
    return nullptr;
}

// Build the sensor list from the `sensors` config key. Unknown types are
// logged and skipped; the list is capped at kMaxSensors. Returns an empty
// vector only when `sensors` is empty / "stub" resolves to nothing usable.
// Note: `stub` is a valid (no-hardware) entry — a single "stub" returns one
// StubSensor, preserving the historical default of a disabled sensor task.
inline std::vector<Sensor*> createSensorList()
{
    std::string list = DataStore::getInstance().get_value("sensors", "stub");
    std::vector<std::string> types = parseSensorTypes(list);

    std::vector<Sensor*> out;
    for (const auto& type : types)
    {
        if (out.size() >= kMaxSensors)
        {
            logPrintf("TMP", "sensor '%s' exceeds max of %d — skipped",
                      type.c_str(), kMaxSensors);
            continue;
        }
        Sensor* s = createSensorForType(type);
        if (s == nullptr)
        {
            logPrintf("TMP", "unknown sensor type '%s' — skipped", type.c_str());
            continue;
        }
        out.push_back(s);
    }
    return out;
}