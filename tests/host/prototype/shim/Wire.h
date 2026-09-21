#pragma once
// Host shim: I2C bus stub (no I2C devices in the prototype).
class TwoWire
{
public:
    void begin(int = -1, int = -1) {}
};
extern TwoWire Wire;
