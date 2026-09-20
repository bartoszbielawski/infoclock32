#pragma once
#ifndef INFOCLOCK32_LIFE_HPP
#define INFOCLOCK32_LIFE_HPP

// Conway's Game of Life (B3/S23) on an 8-row grid with horizontal wrap —
// the shape of the LED matrix. Pure data-in/data-out so it is testable on
// the host; rendering lives in the task.

#include <cstdint>
#include <cstring>
#include <algorithm>

// cur/nxt are row-major cells[ y * width + x ], exactly width*8 bytes each.
// The buffers must not alias (nxt is memset first). Returns the population of `nxt`.
inline int life_step(const uint8_t* cur, uint8_t* nxt, int width)
{
    std::memset(nxt, 0, (size_t)width * 8);
    int pop = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < width; x++)
        {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++)
            {
                int yy = y + dy;
                if (yy < 0 || yy > 7) continue;
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (dx == 0 && dy == 0) continue;
                    int xx = (x + dx + width) % width;
                    n += cur[yy * width + xx];
                }
            }
            int alive = cur[y * width + x] ? (n == 2 || n == 3) : (n == 3);
            nxt[y * width + x] = (uint8_t)alive;
            pop += alive;
        }
    return pop;
}

// Rolling "has the board stopped producing anything new?" detector.
//
// Comparing against the previous generation alone only catches still lifes;
// random soup on an 8-row grid usually settles into blinkers and other short
// oscillators instead, which look just as dead but never repeat back-to-back.
// This keeps a ring of the last kLifeCycleWindow state hashes, so a repeat at
// distance p means an oscillator of period p (p == 1 being a still life).
// Patterns that travel (gliders) only repeat after crossing the full width,
// well outside the window, so they are left running.
static constexpr int kLifeCycleWindow = 32;

class LifeCycleDetector
{
public:
    void reset() { head_ = 0; count_ = 0; }

    // Feeds one generation. Returns its cycle period in generations
    // (1..kLifeCycleWindow) when this exact state was already seen inside the
    // window, or 0 while the board is still producing new states.
    int observe(const uint8_t* cells, size_t size)
    {
        uint64_t h = hash(cells, size);
        int period = 0;
        for (int i = 1; i <= count_; i++)
        {
            int idx = (head_ - i + kLifeCycleWindow) % kLifeCycleWindow;
            if (ring_[idx] == h) { period = i; break; }
        }
        ring_[head_] = h;
        head_  = (head_ + 1) % kLifeCycleWindow;
        if (count_ < kLifeCycleWindow) count_++;
        return period;
    }

private:
    // FNV-1a over the cell bytes. 64 bits keeps an accidental collision (one
    // needless reseed) far rarer than anything else that can go wrong here.
    static uint64_t hash(const uint8_t* cells, size_t size)
    {
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < size; i++)
        {
            h ^= cells[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    uint64_t ring_[kLifeCycleWindow] = {};
    int      head_  = 0;
    int      count_ = 0;   // live entries, capped at the window size
};

#endif // INFOCLOCK32_LIFE_HPP
