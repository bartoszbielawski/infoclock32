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
        : LEDMatrixDriver(spi, settings, modules, pin_cs, flags),
          spi_(spi), settings_(settings), modules_(modules), cs_(pin_cs) {}

    ~LMDS() {}

    // Full MAX7219 init: every control register, a blank frame, then on.
    // The library constructor sends the mode registers only once, at power-up,
    // and they can be lost (the chips still powering up, a supply dip when the
    // Wi-Fi radio starts). A chip left in display-test mode lights every LED
    // and ignores the frame, so no text ever shows; begin() therefore resends
    // all of them, which also makes /actions "reset display" a real reset.
    void begin() {
        enabled_ = false;
        refreshControl();               // shutdown + test off, decode, scan limit
        clear();
        LEDMatrixDriver::display();
        setEnabled(true);
    }

    // Re-assert the MAX7219 control registers (display test off, no decode,
    // all 8 digits scanned, intensity, shutdown) from the tracked state. Each
    // write is its own SPI transaction, so it never splits a frame. Cheap
    // (~5 x 128 bits) and idempotent; display() calls it once a second, and
    // the housekeeping task while nobody draws (boot, setup portal).
    void refreshControl() {
        lastRefreshMs_ = millis();
        sendAll(kTest);
        sendAll(kDecode);
        sendAll(kScanLimit | 7);
        sendAll(kIntensity | intensity_.load());
        sendAll(kShutdown | (enabled_.load() ? 1 : 0));
    }

    // Shadow the library setters so refreshControl() resends what is current.
    void setIntensity(uint8_t level) {
        intensity_ = level > 15 ? 15 : level;
        LEDMatrixDriver::setIntensity(intensity_);
    }
    void setEnabled(bool enabled) {
        enabled_ = enabled;
        LEDMatrixDriver::setEnabled(enabled);
    }

    // Brightness and power changes requested by tasks that don't hold the
    // display (MQTT, web actions, night mode). They are only recorded here;
    // the current holder sends them on its next display() call, so the
    // MAX7219 only ever sees frame traffic from one task and the requester
    // never has to wait for (or preempt) the display.
    void requestIntensity(uint8_t level) { pendingIntensity_ = level > 15 ? 15 : level; }
    void requestEnabled(bool enabled)    { pendingEnabled_ = enabled ? 1 : 0; }

    // Shadows LEDMatrixDriver::display() to apply pending requests first.
    void display() {
        applyPending();
        if (millis() - lastRefreshMs_.load() >= kRefreshMs)
            refreshControl();
        LEDMatrixDriver::display();
    }

    // For the housekeeping task while nobody holds the display: control
    // registers only, the frame is left alone.
    void refreshIdle() {
        applyPending();
        refreshControl();
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
    // MAX7219 register addresses (the library keeps its copies private).
    static constexpr uint16_t kDecode    = 0x0900;
    static constexpr uint16_t kIntensity = 0x0A00;
    static constexpr uint16_t kScanLimit = 0x0B00;
    static constexpr uint16_t kShutdown  = 0x0C00;
    static constexpr uint16_t kTest      = 0x0F00;
    static constexpr uint32_t kRefreshMs = 1000;

    void applyPending() {
        int v = pendingIntensity_.exchange(-1);
        if (v >= 0) setIntensity((uint8_t)v);
        v = pendingEnabled_.exchange(-1);
        if (v >= 0) setEnabled(v != 0);
    }

    // Same framing as the library's _sendCommand(): one word per module.
    void sendAll(uint16_t cmd) {
        spi_.beginTransaction(settings_);
        digitalWrite(cs_, 0);
        for (uint8_t i = 0; i < modules_; ++i)
            spi_.transfer16(cmd);
        digitalWrite(cs_, 1);
        spi_.endTransaction();
    }

    SPIClass&   spi_;
    SPISettings settings_;
    uint8_t     modules_;
    uint8_t     cs_;
    std::atomic<uint8_t>  intensity_{8};
    std::atomic<bool>     enabled_{false};
    std::atomic<uint32_t> lastRefreshMs_{0};
    std::atomic<int> pendingIntensity_{-1};   // -1 = nothing pending
    std::atomic<int> pendingEnabled_{-1};
};


#endif // LMDS_HPP
