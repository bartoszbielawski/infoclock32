#pragma once
// Pure timing model of scrollMessage() (src/graphic_utils.cpp, scrollCanvas
// with holdMs < 0), so a caller can size the display hold a scroll needs.
// Host-tested in tests/host/scroll_timing_check.cpp.

#include <cstddef>
#include <cstdint>

static constexpr int kScrollFontWidthPx = 6;   // 5 px glyph + 1 px gap

// Wall time of one scroll of `chars` characters at `speed` ms per step:
// 10*speed lead-in, 50*speed pause on the first frame, one frame per step,
// 50*speed pause on the last frame. A message that fits is shown centered
// for 100*speed.
inline uint32_t scroll_duration_ms(size_t chars, int speed, int displayWidthPx = 64, int steps = 1)
{
    if (speed < 0) speed = 0;
    if (steps < 1) steps = 1;
    const long canvas = (long)chars * kScrollFontWidthPx;
    if (canvas <= displayWidthPx)
        return (uint32_t)(100L * speed);
    const long last   = canvas - displayWidthPx + steps;
    const long frames = last / steps + 1;
    return (uint32_t)((10L + 50L + 50L + frames) * speed);
}

// Minimum slice for a hold that should finish its scroll before the clock
// may cut in: the scroll time plus `marginMs`, never below `floorMs` (the
// shared display_min_hold_s policy) and never above `capMs`, so a long
// message still cannot keep the clock away indefinitely.
inline uint32_t scroll_slice_ms(uint32_t scrollMs, uint32_t floorMs,
                                uint32_t capMs = 30000, uint32_t marginMs = 1000)
{
    uint32_t want = scrollMs + marginMs;
    if (want < floorMs) want = floorMs;
    if (want > capMs)   want = capMs > floorMs ? capMs : floorMs;
    return want;
}
