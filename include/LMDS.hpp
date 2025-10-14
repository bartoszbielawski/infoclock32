#ifndef LMDS_HPP
#define LMDS_HPP

#include <LEDMatrixDriver.hpp>

class LMDS : public LEDMatrixDriver
{
public:
    LMDS(uint8_t modules, uint8_t pin_cs) : LEDMatrixDriver(modules, pin_cs) {}
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