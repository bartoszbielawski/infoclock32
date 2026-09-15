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

#endif // INFOCLOCK32_LIFE_HPP
