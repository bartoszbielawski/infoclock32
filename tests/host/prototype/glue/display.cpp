#include "display.hpp"

#include <Arduino.h>
#include <SPI.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace host_display
{
namespace
{
int segments_ = 8;
int csPin_ = 7;
bool ansi_ = true;

// framebuffer in LEDMatrixDriver layout: fb[row][controller]
uint8_t fb_[8][64];
std::vector<uint16_t> pending_;
std::chrono::steady_clock::time_point lastRender_;
std::mutex renderMutex_;

void renderNow()
{
    std::string frame;
    frame.reserve(16 * 64 + 128);
    frame += "\n-- infoclock32 host prototype -- 64x8 --\n";
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < segments_ * 8; x++)
            frame += (fb_[y][x >> 3] >> (x & 7)) & 1 ? '#' : ' ';
        frame += '\n';
    }
    if (ansi_) frame = "\033[2J\033[H" + frame;

    // one locked write per frame so log lines don't split the picture
    Serial.write((const uint8_t*)frame.data(), frame.size());
}

void latch()
{
    uint8_t reg = 0;
    for (size_t j = 0; j < pending_.size() && j < 64; j++)
    {
        uint16_t w = pending_[j];
        reg = w >> 8;
        uint8_t data = w & 0xFF;
        if (reg >= 1 && reg <= 8) fb_[reg - 1][j] = data;
    }
    pending_.clear();

    // render once the whole frame (last row register) has been latched
    if (reg == 8)
    {
        auto now = std::chrono::steady_clock::now();
        if (now - lastRender_ > std::chrono::milliseconds(250))
        {
            lastRender_ = now;
            renderNow();
        }
    }
}

void spiWord(uint16_t value) { pending_.push_back(value); }

void gpioWrite(int pin, int value)
{
    if (pin != csPin_) return;
    if (value)
        latch();
    else
        pending_.clear();
}
}  // namespace

void init(int segments, int csPin, bool ansi)
{
    segments_ = segments;
    csPin_ = csPin;
    ansi_ = ansi;
    SPI.setTransfer16Hook(spiWord);
    host_set_gpio_hook(gpioWrite);
}
}  // namespace host_display
