#pragma once

// Board pin definitions — one section per ESP32 variant.
// Uses ESP-IDF chip-level macros (CONFIG_IDF_TARGET_*) which are always
// defined by the build system regardless of the specific board variant.

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// ESP32-C3 SuperMini hardware SPI: SCK=GPIO4, MOSI=GPIO6, CS=GPIO7.
#  define LED_BLINK_PIN    8
#  define MATRIX_CS_PIN    7
#  define MATRIX_SCK_PIN   4   // FSPI SCK
#  define MATRIX_MOSI_PIN  6   // FSPI MOSI
#  define I2C_SDA_PIN      1
#  define I2C_SCL_PIN      0

#elif defined(CONFIG_IDF_TARGET_ESP32S2)
// ESP32-S2 Saola-1: no simple GPIO LED on board; adjust LED_BLINK_PIN as needed
#  define LED_BLINK_PIN    15
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   36  // SPI2 SCK on Saola
#  define MATRIX_MOSI_PIN  35  // SPI2 MOSI on Saola
#  define I2C_SDA_PIN      8
#  define I2C_SCL_PIN      9

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3 DevKitC-1: RGB (WS2812) on GPIO48; GPIO2 used for simple blink
#  define LED_BLINK_PIN    2
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   12  // FSPI SCK
#  define MATRIX_MOSI_PIN  11  // FSPI MOSI
#  define I2C_SDA_PIN      8
#  define I2C_SCL_PIN      9

#elif defined(CONFIG_IDF_TARGET_ESP32)
// Original ESP32 DevKit v1: blue LED on GPIO2
#  define LED_BLINK_PIN    2
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   18  // VSPI SCK
#  define MATRIX_MOSI_PIN  23  // VSPI MOSI
#  define I2C_SDA_PIN      21  // default ESP32 I2C SDA
#  define I2C_SCL_PIN      22  // default ESP32 I2C SCL
#  define BMP280_GND_PIN   11  // GPIO driven LOW as substitute GND for BMP280

#else
#  error "Unknown ESP32 target — add pin definitions for your board in include/pins.hpp"
#endif
