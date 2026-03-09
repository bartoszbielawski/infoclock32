#pragma once

// Board pin definitions — one section per ESP32 variant.
// Uses ESP-IDF chip-level macros (CONFIG_IDF_TARGET_*) which are always
// defined by the build system regardless of the specific board variant.

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// ESP32-C3 DevKitM-1: RISC-V, built-in LED on GPIO8
#  define LED_BLINK_PIN    8
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   6   // FSPI SCK
#  define MATRIX_MOSI_PIN  7   // FSPI MOSI

#elif defined(CONFIG_IDF_TARGET_ESP32S2)
// ESP32-S2 Saola-1: no simple GPIO LED on board; adjust LED_BLINK_PIN as needed
#  define LED_BLINK_PIN    15
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   36  // SPI2 SCK on Saola
#  define MATRIX_MOSI_PIN  35  // SPI2 MOSI on Saola

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3 DevKitC-1: RGB (WS2812) on GPIO48; GPIO2 used for simple blink
#  define LED_BLINK_PIN    2
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   12  // FSPI SCK
#  define MATRIX_MOSI_PIN  11  // FSPI MOSI

#elif defined(CONFIG_IDF_TARGET_ESP32)
// Original ESP32 DevKit v1: blue LED on GPIO2
#  define LED_BLINK_PIN    2
#  define MATRIX_CS_PIN    5
#  define MATRIX_SCK_PIN   18  // VSPI SCK
#  define MATRIX_MOSI_PIN  23  // VSPI MOSI

#else
#  error "Unknown ESP32 target — add pin definitions for your board in include/pins.hpp"
#endif
