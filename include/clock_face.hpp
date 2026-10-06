#pragma once
#ifndef CLOCK_FACE_HPP
#define CLOCK_FACE_HPP

#include <stdint.h>

// Pure layout helpers for the clock faces driven by the `clock_style` config
// key. No Arduino/FreeRTOS — unit tested on the host
// (see tests/host/clock_face_check.cpp). The draw functions take any
// display-like object with setPixel(x, y, bool) — LMDS on device, a fake
// pixel grid in tests.

// ── Bold digit font ──────────────────────────────────────────────────────────
// Hand-tuned 6x8 digit bitmaps for the `bold` clock face: 2 px strokes, real
// glyph shapes (not a 7-seg emulation), drawn with drawBoldTime().

// One byte per row, bit 5 = left column ... bit 0 = right column.
// Free-standing outer corners are cut (see 0, 2, 5, 7) so the bars chamfer
// into the strokes instead of ending in square blocks.
static const uint8_t kBoldDigits[10][8] = {
    // 0: rounded stadium, hollow center
    { 0x1E, 0x3F, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x1E },
    // 1: left flag at the top, plain stem to the bottom
    { 0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C },
    // 2: rounded bowl, diagonal to the lower left, rounded foot
    { 0x1E, 0x33, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x1E },
    // 3: two bowls with a middle notch
    { 0x1E, 0x33, 0x03, 0x0E, 0x03, 0x03, 0x33, 0x1E },
    // 4: diagonal, full crossbar, center stem
    { 0x06, 0x0E, 0x1E, 0x36, 0x3F, 0x0C, 0x0C, 0x0C },
    // 5: rounded top bar, left stem, full middle bar, lower bowl
    { 0x1E, 0x30, 0x30, 0x3F, 0x03, 0x33, 0x33, 0x1E },
    // 6: closed top hook, left stem, full middle bar, bowl
    { 0x1E, 0x33, 0x30, 0x3F, 0x33, 0x33, 0x33, 0x1E },
    // 7: rounded top bar, diagonal into a straight stem
    { 0x1E, 0x03, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x0C },
    // 8: two stacked bowls with a pinched waist
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x33, 0x1E },
    // 9: closed bowl, middle bar, right stem, tail
    { 0x1E, 0x33, 0x33, 0x1F, 0x03, 0x03, 0x33, 0x1E },
};

template<class D>
inline void drawBoldDigit(D& d, int digit, int x0, int y0)
{
    if (digit < 0 || digit > 9) return;
    for (int r = 0; r < 8; r++)
    {
        const uint8_t bits = kBoldDigits[digit][r];
        for (int c = 0; c < 6; c++)
            if (bits & (1 << (5 - c))) d.setPixel(x0 + c, y0 + r, true);
    }
}

// hh:mm in bold digits: two digits, 1 px gap, 2 px colon, 1 px gap, two digits.
// x offsets within the face: digits 0, 7, colon 14-15, digits 17, 24.
static const int kBoldFaceWidth = 30; // 4*6 digit px + 4 gaps + 2 colon px

template<class D>
inline void drawBoldTime(D& d, int h, int m, bool colonOn, int x0, int y0)
{
    drawBoldDigit(d, (h / 10) % 10, x0, y0);
    drawBoldDigit(d, h % 10, x0 + 7, y0);
    if (colonOn)
    {
        for (int r = 2; r <= 3; r++)            // upper 2x2 block
            for (int i = 0; i <= 1; i++)
                d.setPixel(x0 + 14 + i, y0 + r, true);
        for (int r = 5; r <= 6; r++)            // lower 2x2 block
            for (int i = 0; i <= 1; i++)
                d.setPixel(x0 + 14 + i, y0 + r, true);
    }
    drawBoldDigit(d, m / 10, x0 + 17, y0);
    drawBoldDigit(d, m % 10, x0 + 24, y0);
}

// ── BCD (binary) clock ───────────────────────────────────────────────────────
// Four dot columns (hours tens/units, minutes tens/units), bit i at
// (bottom - i): bit 0 on the bottom row.

inline uint8_t bcdColumnBits(int value, int bits)
{
    uint8_t mask = 0;
    for (int i = 0; i < bits && i < 8; i++)
        if (value & (1 << i)) mask |= (1 << i);
    return mask;
}

// Column pitch 3 px: HT HU : MT MU → x offsets 0, 3, 6, 9, 12.
static const int kBCDFaceWidth = 13;

template<class D>
inline void drawBCDTime(D& d, int h, int m, bool colonOn, int x0, int y0)
{
    const int bottom = y0 + 7;   // bit 0 row when y0 == 0
    const uint8_t ht = bcdColumnBits((h / 10) % 10, 2);
    const uint8_t hu = bcdColumnBits(h % 10, 4);
    const uint8_t mt = bcdColumnBits(m / 10, 3);
    const uint8_t mu = bcdColumnBits(m % 10, 4);
    for (int i = 0; i < 2; i++) if (ht & (1 << i)) d.setPixel(x0 +  0, bottom - i, true);
    for (int i = 0; i < 4; i++) if (hu & (1 << i)) d.setPixel(x0 +  3, bottom - i, true);
    if (colonOn)
    {
        d.setPixel(x0 + 6, y0 + 2, true);
        d.setPixel(x0 + 6, y0 + 4, true);
    }
    for (int i = 0; i < 3; i++) if (mt & (1 << i)) d.setPixel(x0 +  9, bottom - i, true);
    for (int i = 0; i < 4; i++) if (mu & (1 << i)) d.setPixel(x0 + 12, bottom - i, true);
}

// ── Seconds sweep ────────────────────────────────────────────────────────────
// One pixel racing along the bottom row: 0 s at the left edge, 59 s at maxX.
// The WiFi indicator owns the very last column, so callers pass
// maxX = display width - 2 (62 on a 64 px display).

inline int sweepX(int sec, int maxX)
{
    if (sec < 0) sec = 0;
    if (sec > 59) sec = 59;
    return sec * maxX / 59;
}

#endif // CLOCK_FACE_HPP
