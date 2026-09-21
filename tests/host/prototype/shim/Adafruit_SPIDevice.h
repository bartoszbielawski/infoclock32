#pragma once
// Host shim: minimal stub for the Adafruit BusIO SPI device header.
#include <stdint.h>

typedef enum { AdafruitSPIDeviceRead = 0 } AdafruitSPIDeviceMode_t;

class SPIClass;
class SPISettings;

class Adafruit_SPIDevice
{
public:
    Adafruit_SPIDevice(int8_t, uint32_t = 1000000, int = 1, int = 0, SPIClass* = nullptr) {}
};
