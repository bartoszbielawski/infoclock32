#pragma once
// Host shim: minimal stubs for the Adafruit BusIO headers that Adafruit_GFX.h
// includes unconditionally. Only Adafruit_GFX (canvas + text rendering) is
// actually used by the firmware on the host.
#include "Wire.h"
#include <Adafruit_SPIDevice.h>

class Adafruit_I2CDevice
{
public:
    Adafruit_I2CDevice(uint8_t, TwoWire* = nullptr) {}
};
