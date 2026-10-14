#pragma once

// The scheduling/merging core of the sensor task, free of FreeRTOS, display
// and stores, so the whole poll->merge->stale cycle can be driven with a
// virtual clock in host tests (tests/host/sensor_round_check.cpp).
//
// The task owns the I/O: it calls step() with the monotonic time, writes the
// RuntimeStore keys named by Result::accepted / Result::dropped, and scrolls.

#include <cstdint>
#include <vector>
#include <sensor_merge.hpp>

class SensorRound
{
public:
    // `list` must already be pruned (see pruneSensors()) and outlive this.
    explicit SensorRound(std::vector<Sensor*>& list) : list_(list)
    {
        data.setOwners(list_);
    }

    struct Result
    {
        bool     polled   = false;   // a sensor was read this step
        bool     ok       = false;   // ... and the read succeeded
        size_t   index    = 0;       // which sensor
        unsigned accepted = 0;       // measurementBit mask now updated
        unsigned dropped  = 0;       // measurementBit mask that went stale
    };

    // Advance the round at monotonic time `now` (seconds).
    Result step(int64_t now, int intervalSeconds, const SensorOffsets& off)
    {
        Result r;
        const size_t n = list_.size();
        if (n == 0) return r;

        const int64_t staleAfter = staleAfterSeconds(n, intervalSeconds);

        if (now >= nextPollAt_)
        {
            r.polled = true;
            r.index = rr_;
            Sensor* s = list_[rr_];
            rr_ = (rr_ + 1) % n;

            if (s->read())
            {
                r.ok = true;
                nextPollAt_ = now + intervalSeconds;
                r.accepted = data.ingest(*s, (int)r.index, now, staleAfter, off);
            }
            else
            {
                // Try the next sensor sooner than a full interval.
                nextPollAt_ = now + (intervalSeconds < 30 ? intervalSeconds : 30);
            }
        }

        r.dropped = data.sweepStale(now, staleAfter);
        return r;
    }

    int64_t nextPollAt() const { return nextPollAt_; }

    MeasurementSet data;

private:
    std::vector<Sensor*>& list_;
    size_t  rr_ = 0;
    int64_t nextPollAt_ = 0;   // 0 => first poll immediately
};
