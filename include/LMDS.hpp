#ifndef LMDS_HPP
#define LMDS_HPP

#include <atomic>
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

    // Brightness and power changes requested by tasks that don't hold the
    // display (MQTT, web actions, night mode). They are only recorded here;
    // the current holder sends them on its next display() call, so the
    // MAX7219 only ever sees SPI traffic from one task and the requester
    // never has to wait for (or preempt) the display.
    void requestIntensity(uint8_t level) { pendingIntensity_ = level > 15 ? 15 : level; }
    void requestEnabled(bool enabled)    { pendingEnabled_ = enabled ? 1 : 0; }

    // Shadows LEDMatrixDriver::display() to apply pending requests first.
    void display() {
        int v = pendingIntensity_.exchange(-1);
        if (v >= 0) setIntensity((uint8_t)v);
        v = pendingEnabled_.exchange(-1);
        if (v >= 0) setEnabled(v != 0);
        LEDMatrixDriver::display();
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

private:
    std::atomic<int> pendingIntensity_{-1};   // -1 = nothing pending
    std::atomic<int> pendingEnabled_{-1};
};


#endif // LMDS_HPP