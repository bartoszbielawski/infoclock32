#pragma once

// Small timing helpers (pure, host-tested in tests/host/tick_utils_check.cpp).

#include <cstdint>

// Milliseconds to sleep after drawing a clock frame so the next frame starts
// just after the second that follows the one this frame showed. Both times are
// microseconds since the epoch (gettimeofday). The clock face used to draw
// and then sleep a flat 1000 ms, so drawing time accumulated and a second was
// skipped every so often. Counting from the frame's start (not from when the
// drawing finished) also covers a frame that began late in its second: the
// next one still shows the very next second. 0 if drawing overran.
inline uint32_t msToNextFrame(int64_t frameStartUs, int64_t nowUs, uint32_t marginMs = 10)
{
    const int64_t nextBoundary = (frameStartUs / 1000000 + 1) * 1000000;
    const int64_t wakeUs = nextBoundary + (int64_t)marginMs * 1000;
    if (nowUs >= wakeUs) return 0;
    return (uint32_t)((wakeUs - nowUs + 999) / 1000);
}

// Coalesces bursts of changes into one later action, e.g. saving the config
// once after a Home Assistant brightness slider stops moving instead of
// rewriting the file for every step. Times are monotonic seconds.
class DeferredAction
{
public:
    // A change happened: act `delayS` seconds after the last one.
    void touch(int64_t now, int delayS) { dueAt_ = now + delayS; pending_ = true; }

    // True once when the quiet period is over.
    bool takeDue(int64_t now)
    {
        if (!pending_ || now < dueAt_) return false;
        pending_ = false;
        return true;
    }

    bool pending() const { return pending_; }

private:
    int64_t dueAt_ = 0;
    bool    pending_ = false;
};
