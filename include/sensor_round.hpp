#pragma once

// The scheduling/merging core of the sensor task, free of FreeRTOS, display
// and stores, so the whole poll->merge->stale cycle can be driven with a
// virtual clock in host tests (tests/host/sensor_round_check.cpp).
//
// The task owns the I/O: it calls step() with the monotonic time, writes the
// RuntimeStore keys named by Result::accepted / Result::dropped, and scrolls.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <sensor_merge.hpp>

class SensorRound
{
public:
    // `list` must already be pruned (see pruneSensors()) and outlive this.
    explicit SensorRound(std::vector<Sensor*>& list)
        : list_(list), fastNext_(list.size(), 0)
    {
        data.setOwners(list_);
        for (size_t i = 0; i < list_.size(); i++)
            if (list_[i]->pollIntervalS() <= 0) hasSlow_ = true;
    }

    struct Result
    {
        bool     polled   = false;   // a sensor was read this step
        bool     ok       = false;   // ... and the read succeeded
        size_t   index    = 0;       // which sensor
        unsigned accepted = 0;       // measurementBit mask now updated
        unsigned dropped  = 0;       // measurementBit mask that went stale
    };

    // Advance the round at monotonic time `now` (seconds). At most one sensor
    // is read per call. Sensors with their own pollIntervalS() (fast ones) are
    // scheduled independently and take precedence; the rest go round-robin at
    // `intervalSeconds`.
    Result step(int64_t now, int intervalSeconds, const SensorOffsets& off)
    {
        Result r;
        const size_t n = list_.size();
        if (n == 0) return r;

        const int64_t staleAfter = staleAfterSeconds(n, intervalSeconds);

        bool pick = false;
        size_t idx = 0;
        bool fast = false;

        for (size_t i = 0; i < n && !pick; i++)
        {
            if (list_[i]->pollIntervalS() > 0 && now >= fastNext_[i])
            {
                pick = true; idx = i; fast = true;
            }
        }
        if (!pick && hasSlow_ && now >= nextPollAt_)
        {
            while (list_[rr_]->pollIntervalS() > 0) rr_ = (rr_ + 1) % n;
            pick = true; idx = rr_;
            rr_ = (rr_ + 1) % n;
        }

        if (pick)
        {
            r.polled = true;
            r.index = idx;
            Sensor* s = list_[idx];
            bool ok = s->read();

            if (fast)
                fastNext_[idx] = now + s->pollIntervalS();
            else
                nextPollAt_ = now + (ok ? intervalSeconds
                                        // Try the next sensor sooner than a full interval.
                                        : (intervalSeconds < 30 ? intervalSeconds : 30));

            if (ok)
            {
                r.ok = true;
                r.accepted = data.ingest(*s, (int)idx, now, staleAfter, off);
            }
        }

        r.dropped = data.sweepStale(now, staleAfter);
        return r;
    }

    // Monotonic time of the earliest sensor poll that is due next.
    int64_t nextPollAt() const
    {
        int64_t t = hasSlow_ ? nextPollAt_ : INT64_MAX;
        for (size_t i = 0; i < list_.size(); i++)
            if (list_[i]->pollIntervalS() > 0 && fastNext_[i] < t) t = fastNext_[i];
        return t == INT64_MAX ? nextPollAt_ : t;
    }

    MeasurementSet data;

private:
    std::vector<Sensor*>& list_;
    std::vector<int64_t> fastNext_;   // per-sensor next poll, fast sensors only
    bool    hasSlow_ = false;         // any sensor in the round-robin
    size_t  rr_ = 0;
    int64_t nextPollAt_ = 0;   // 0 => first poll immediately
};
