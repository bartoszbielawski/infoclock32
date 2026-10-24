#pragma once

// Full-bus I2C scan for the boot log — pure (no Arduino / Wire), host-tested in
// tests/host/i2c_scan_check.cpp. Independent of sensor autodetection: it
// reports every device that answers, including chips the firmware has no driver
// for (RTCs, displays, expanders ...), so wiring problems and unknown boards
// can be diagnosed from /log.
//
// `Bus` needs `bool ack(uint8_t addr)` (see WireBus in sensor_factory.hpp).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Usable 7-bit address range (0x00-0x07 and 0x78-0x7F are reserved).
inline constexpr uint8_t kI2cFirst = 0x08;
inline constexpr uint8_t kI2cLast  = 0x77;

template <class Bus>
std::vector<uint8_t> scanI2c(Bus& bus)
{
    std::vector<uint8_t> found;
    for (int a = kI2cFirst; a <= kI2cLast; a++)
        if (bus.ack((uint8_t)a)) found.push_back((uint8_t)a);
    return found;
}

// Common chips at an address, for the log. Informational guesses only: many
// parts share addresses, so this lists candidates, never a verdict.
inline const char* i2cCandidates(uint8_t a)
{
    switch (a)
    {
        case 0x10: return "VEML7700/VEML6030";
        case 0x1E: return "HMC5883/LIS3MDL";
        case 0x23: return "BH1750";
        case 0x29: return "TSL2591/VL53L0X/BNO055";
        case 0x38: return "AHT10/AHT20/FT6206";
        case 0x39: return "TSL2561/APDS9960";
        case 0x3C: case 0x3D: return "SSD1306/SH1106 OLED";
        case 0x40: return "INA219/HTU21/SI7021/PCA9685";
        case 0x44: case 0x45: return "SHT3x/INA219";
        case 0x48: case 0x49: case 0x4A: case 0x4B: return "ADS1115/TMP102/PCF8591";
        case 0x57: return "AT24C EEPROM/MAX30102";
        case 0x5A: return "CCS811/MLX90614/MPR121";
        case 0x5C: return "AM2320/BH1750";
        case 0x62: return "SCD4x";
        case 0x68: return "DS3231/DS1307/MPU6050";
        case 0x69: return "MPU6050/BMI160";
        case 0x70: return "TCA9548A/HT16K33";
        case 0x76: case 0x77: return "BMP280/BME280/BMP180/MS5611";
        default:
            if (a >= 0x20 && a <= 0x27) return "PCF8574/MCP23017";
            if (a >= 0x50 && a <= 0x56) return "AT24C EEPROM";
            if (a >= 0x71 && a <= 0x75) return "HT16K33/TCA9548A";
            return "";
    }
}

inline std::string i2cHex(uint8_t a)
{
    char b[8];
    snprintf(b, sizeof b, "0x%02X", a);
    return b;
}

// "0x38 AHT10/AHT20/FT6206" or just "0x7A" for an address we have no idea about.
inline std::string describeI2c(uint8_t a)
{
    std::string s = i2cHex(a);
    const char* c = i2cCandidates(a);
    if (*c) { s += ' '; s += c; }
    return s;
}

// One log line per device keeps each line short; a single line for an empty bus.
inline std::vector<std::string> formatScanLines(const std::vector<uint8_t>& found)
{
    std::vector<std::string> out;
    if (found.empty())
    {
        out.push_back("i2c scan: no devices answered");
        return out;
    }
    char head[32];
    snprintf(head, sizeof head, "i2c scan: %zu device(s)", found.size());
    out.push_back(head);
    for (uint8_t a : found) out.push_back("  " + describeI2c(a));
    return out;
}

// Comma list for the `i2c_devices` runtime value: "0x38,0x76".
inline std::string scanSummary(const std::vector<uint8_t>& found)
{
    std::string s;
    for (uint8_t a : found) { if (!s.empty()) s += ','; s += i2cHex(a); }
    return s;
}
