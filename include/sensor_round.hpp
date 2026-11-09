#pragma once

// The scheduling/merging core of the sensor task, free of FreeRTOS, display
// and stores, so the whole poll->merge->stale cycle can be driven with a
// virtual clock in host tests (tests/host/sim_sensor_check.cpp).
//
// Every sensor has its own timer: sensors with pollIntervalS() > 0 (ambient
// light) use that, the rest `sensor_interval`. Each step first starts every
// due sensor (Sensor::startMeasurement) and then collects every sensor whose
// conversion is ready, so slow conversions overlap instead of adding up and
// each sensor refreshes once per interval however many there are.
//
// The task owns the I/O: it calls step() with the monotonic time, writes the
// RuntimeStore keys named by the reads' `accepted` and Result::dropped, and
// sleeps until nextPollAt().

#include <cstdint>
#include <cstddef>
#include <vector>
#include <sensor_merge.hpp>

class SensorRound
{
public:
    // `list` must already be pruned (see pruneSensors()) and outlive this.
    explicit SensorRound(std::vector<Sensor*>& list)
        : list_(list), next_(list.size(), 0), readyAt_(list.size(), 0),
          startedAt_(list.size(), 0), pending_(list.size(), false)
    {
        data.setOwners(list_);
    }

    struct Read
    {
        size_t   index    = 0;       // which sensor
        bool     ok       = false;   // the read succeeded
        unsigned accepted = 0;       // measurementBit mask now updated
    };

    struct Result
    {
        std::vector<Read> reads;     // sensors read (or failed) in this step
        size_t   started  = 0;       // conversions started, collected later
        unsigned dropped  = 0;       // measurementBit mask that went stale
    };

    // Advance at monotonic time `now` (seconds).
    Result step(int64_t now, int intervalSeconds, const SensorOffsets& off)
    {
        Result r;
        const size_t n = list_.size();
        if (n == 0) return r;

        const int64_t staleAfter = staleAfterSeconds(intervalSeconds);

        // 1) Start every due, idle sensor. A sensor without a separate start
        //    (0 ms) is read right away; a failed start counts as a failed read.
        for (size_t i = 0; i < n; i++)
        {
            if (pending_[i] || now < next_[i]) continue;
            Sensor* s = list_[i];
            const int ms = s->startMeasurement();
            startedAt_[i] = now;
            if (ms < 0)
            {
                r.reads.push_back(finish(i, now, false, intervalSeconds, staleAfter, off));
            }
            else if (ms == 0)
            {
                bool ok = s->read();
                r.reads.push_back(finish(i, now, ok, intervalSeconds, staleAfter, off));
            }
            else
            {
                pending_[i] = true;
                readyAt_[i] = now + (ms + 999) / 1000;   // whole seconds, rounded up
                r.started++;
            }
        }

        // 2) Collect every conversion that is ready.
        for (size_t i = 0; i < n; i++)
        {
            if (!pending_[i] || now < readyAt_[i]) continue;
            pending_[i] = false;
            bool ok = list_[i]->read();
            r.reads.push_back(finish(i, now, ok, intervalSeconds, staleAfter, off));
        }

        r.dropped = data.sweepStale(now, staleAfter);
        return r;
    }

    // Monotonic time of the next start or collection that is due.
    int64_t nextPollAt() const
    {
        int64_t t = INT64_MAX;
        for (size_t i = 0; i < list_.size(); i++)
        {
            int64_t due = pending_[i] ? readyAt_[i] : next_[i];
            if (due < t) t = due;
        }
        return t == INT64_MAX ? 0 : t;
    }

    MeasurementSet data;

private:
    int periodOf(size_t i, int intervalSeconds) const
    {
        int p = list_[i]->pollIntervalS();
        return p > 0 ? p : intervalSeconds;
    }

    // Schedule the sensor's next measurement and merge a good reading. The
    // cadence counts from when the measurement started; after a failure a
    // round sensor retries sooner than a full interval (at most 30 s).
    Read finish(size_t i, int64_t now, bool ok, int intervalSeconds,
                int64_t staleAfter, const SensorOffsets& off)
    {
        Read rd;
        rd.index = i;
        rd.ok = ok;
        const int period = periodOf(i, intervalSeconds);
        const bool fast = list_[i]->pollIntervalS() > 0;
        const int retry = fast ? period : (intervalSeconds < 30 ? intervalSeconds : 30);
        next_[i] = startedAt_[i] + (ok ? period : retry);
        if (next_[i] <= now) next_[i] = now + 1;   // never re-poll in the same second
        if (ok)
            rd.accepted = data.ingest(*list_[i], (int)i, now, staleAfter, off);
        return rd;
    }

    std::vector<Sensor*>& list_;
    std::vector<int64_t>  next_;       // when the next measurement starts
    std::vector<int64_t>  readyAt_;    // when a started conversion can be collected
    std::vector<int64_t>  startedAt_;
    std::vector<bool>     pending_;    // conversion started, not collected yet
};
