#pragma once

// Factory that instantiates the right Sensor implementation based on the
// `temp_sensor` config key.  Must be called after DataStore::load_from_file()
// and Wire.begin() (for I2C sensors).
//
// Config key   | Sensor class         | Extra config keys
// -------------|----------------------|-----------------------------------
// bmp180       | Bmp180Sensor         | — (address 0x77, fixed)
// bmp280       | Bmp280Sensor         | bmp280_addr  (default 0x76)
// bme280       | Bme280Sensor         | bme280_addr  (default 0x76)
// sht31        | Sht31Sensor          | sht31_addr   (default 0x44)
// aht10        | AhtSensor            | — (address 0x38, fixed)
// aht20        | AhtSensor            | — (address 0x38, fixed)
// ds18b20      | Ds18b20Sensor        | ds18b20_pin  (default 4)
// scd4x        | Scd4xSensor          | — (address 0x62, fixed; adds CO2)
// stub / <any> | StubSensor           | —
//
// DS18B20 does NOT use I2C — Wire.begin() is still safe to call but has no
// effect on it.

#include <string>
#include <sensor.hpp>
#include <bmp180_sensor.hpp>
#include <bmp280_sensor.hpp>
#include <bme280_sensor.hpp>
#include <sht31_sensor.hpp>
#include <aht_sensor.hpp>
#include <ds18b20_sensor.hpp>
#include <scd4x_sensor.hpp>
#include <data_store.hpp>

inline Sensor* createSensor() {
    auto& ds = DataStore::getInstance();
    std::string type = ds.get_value("temp_sensor", "stub");

    if (type == "bmp180") {
        return new Bmp180Sensor();
    }
    if (type == "bmp280") {
        uint8_t addr = (uint8_t)ds.get_value<int>("bmp280_addr", 0x76);
        return new Bmp280Sensor(addr);
    }
    if (type == "bme280") {
        uint8_t addr = (uint8_t)ds.get_value<int>("bme280_addr", 0x76);
        return new Bme280Sensor(addr);
    }
    if (type == "sht31") {
        uint8_t addr = (uint8_t)ds.get_value<int>("sht31_addr", 0x44);
        return new Sht31Sensor(addr);
    }
    if (type == "aht10") {
        return new AhtSensor(/*isAht20=*/false);
    }
    if (type == "aht20") {
        return new AhtSensor(/*isAht20=*/true);
    }
    if (type == "ds18b20") {
        int pin = ds.get_value<int>("ds18b20_pin", 4);
        return new Ds18b20Sensor((uint8_t)pin);
    }
    if (type == "scd4x") {
        return new Scd4xSensor();
    }

    return new StubSensor();
}
