#ifndef LMDS_HPP
#define LMDS_HPP

#include <SPI.h>
#include <LEDMatrixDriver.hpp>

class LMDS : public LEDMatrixDriver
{
public:
    // Explicit SPI constructor: caller must have called spi.begin() with the
    // desired pins before constructing LMDS (the library will not re-begin a
    // non-default SPIClass instance).
    LMDS(SPIClass& spi, SPISettings settings, uint8_t modules, uint8_t pin_cs, uint8_t flags = 0)
        : LEDMatrixDriver(spi, settings, modules, pin_cs, flags) {}

    ~LMDS() {}

    void begin() {
        setEnabled(true);
        setIntensity(8); // Set medium brightness
        clear();
        display();                
    }

    template <class S>
    void displayToSerial(S& serial) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < getSegments() * 8; x++) {
                serial.print(getPixel(x, y) ? '#' : ' ');
            }
            serial.println();
        }
        serial.println();
    }
};


#endif // LMDS_HPP