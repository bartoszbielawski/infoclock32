#pragma once
// Host shim: SPI bus captured by the MAX7219 display decoder. Transfers never
// touch hardware — the glue layer registers a hook to observe the words.
#include <stdint.h>

typedef enum { MSBFIRST = 1, LSBFIRST = 0 } BitOrder;
typedef enum { SPI_MODE0 = 0, SPI_MODE1, SPI_MODE2, SPI_MODE3 } SpiMode;

class SPISettings
{
public:
    SPISettings() = default;
    SPISettings(uint32_t clock, BitOrder order, SpiMode mode)
        : clock_(clock), order_(order), mode_(mode) {}
    uint32_t clock() const { return clock_; }
    BitOrder order() const { return order_; }
    SpiMode mode() const { return mode_; }

private:
    uint32_t clock_ = 0;
    BitOrder order_ = MSBFIRST;
    SpiMode mode_ = SPI_MODE0;
};

using HostSpiTransfer16Hook = void (*)(uint16_t value);

class SPIClass
{
public:
    void begin(int = -1, int = -1, int = -1) {}
    void beginTransaction(const SPISettings&) {}
    void endTransaction() {}

    uint16_t transfer16(uint16_t value)
    {
        if (hook_) hook_(value);
        return 0;
    }
    uint8_t transfer(uint8_t value)
    {
        transfer16(value);
        return 0;
    }

    void setTransfer16Hook(HostSpiTransfer16Hook hook) { hook_ = hook; }

private:
    HostSpiTransfer16Hook hook_ = nullptr;
};

extern SPIClass SPI;
