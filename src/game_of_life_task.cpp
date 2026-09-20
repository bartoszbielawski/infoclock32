#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <life.hpp>
#include <night_utils.hpp>

static constexpr int DEFAULT_INTERVAL_S = 300;
static constexpr int DEFAULT_BURST_S    = 30;
static constexpr int DEFAULT_MIN_HOLD_S = 10;
static constexpr int GEN_DELAY_MS       = 80;   // ~12 generations per second

// Generations a settled board stays on screen before the reseed, so the
// oscillator that ended it is actually visible (~0.6 s at GEN_DELAY_MS).
static constexpr int SETTLE_DWELL_GENS  = 8;

static void seed_field(uint8_t* cells, size_t size)
{
    for (size_t i = 0; i < size; i++)
        cells[i] = (random(0, 100) < 45) ? 1 : 0;
}

// Runs one animation burst on an already-acquired display, then returns.
// `min_hold_ms` is the slice the burst is allowed to keep before a waiting
// priority-lane request (clock, user push) can cut it short.
static void run_burst(LMDS& display, uint8_t* cur, uint8_t* nxt,
                      int width, int burst_ms, int min_hold_ms)
{
    const size_t size = (size_t)width * 8;
    LifeCycleDetector cycle;
    int dwell = 0;              // >0: settled, counting down to the reseed
    int generation = 0;

    seed_field(cur, size);
    cycle.observe(cur, size);

    unsigned long start = millis();
    while ((long)(millis() - start) < burst_ms)
    {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < width; x++)
                display.setPixel(x, y, cur[y * width + x] != 0);
        display.display();
        ResourceManager<LMDS>::getInstance().renewHold();

        // Filler content: a waiting priority-lane request (clock, user push)
        // ends the burst — but only once the burst has had its slice, since
        // the clock comes back around every few seconds and would otherwise
        // cut every burst short. The animation can cut off anywhere.
        if (ResourceManager<LMDS>::getInstance().yieldRequested() &&
            millis() - start > (unsigned long)min_hold_ms)
        {
            logPrintf("LIFE", "burst preempted after %lu ms",
                      (unsigned long)(millis() - start));
            break;
        }

        int pop = life_step(cur, nxt, width);
        std::memcpy(cur, nxt, size);
        generation++;

        // A board that died back, or one that repeats a state it already
        // showed (still life, blinker, any oscillator up to the detector's
        // window), has nothing left to show — dwell on it briefly, then
        // drop in fresh soup so the rest of the burst stays alive.
        int period = cycle.observe(cur, size);
        if (dwell > 0)
        {
            if (--dwell == 0)
            {
                seed_field(cur, size);
                cycle.reset();
                cycle.observe(cur, size);
                generation = 0;
            }
        }
        else if (pop < 3 || period > 0)
        {
            logPrintf("LIFE", "settled after %d gens (pop %d, period %d) — reseeding",
                      generation, pop, period);
            dwell = SETTLE_DWELL_GENS;
        }

        vTaskDelay(GEN_DELAY_MS / portTICK_PERIOD_MS);
    }
}

// Conway's Game of Life shown in periodic bursts between the clock and the
// other display tasks. Backs off when the display is contended, and stays
// quiet during the configured night window.
void game_of_life_task(void* /*parameter*/)
{
    registerTask("Life", 4096, 60000);
    auto& rmd = ResourceManager<LMDS>::getInstance();
    auto& ds = DataStore::getInstance();

    // Let the boot animations and first clock cycle run before joining in.
    vTaskDelay(30000 / portTICK_PERIOD_MS);

    std::vector<uint8_t> cur, nxt;

    while (true)
    {
        task_heartbeat();
        int interval_s = max(30, min(3600, ds.get_value<int>("life_interval_s", DEFAULT_INTERVAL_S)));
        int burst_s    = max(5,  min(120, ds.get_value<int>("life_burst_s",    DEFAULT_BURST_S)));
        // Guaranteed slice before the clock can preempt the burst; capped at
        // burst_s, and 0 restores the old "yield to the clock immediately".
        int min_hold_s = max(0, min(burst_s, ds.get_value<int>("life_min_hold_s", DEFAULT_MIN_HOLD_S)));

        int width = rmd.getResourceRef().getSegments() * 8;
        if ((size_t)width * 8 != cur.size())
        {
            cur.resize((size_t)width * 8);
            nxt.resize((size_t)width * 8);
        }

        if (!is_night_now(ds, time(nullptr)))
        {
            // The burst holds the display for up to burst_s — extend every
            // task waiting on the display (they cannot beat while blocked).
            // Block (not timeout-acquire): a timed-out request would linger
            // in the manager queue as a dead entry and poison the handshake.
            task_grace_all(burst_s * 1000 + 10000);
            if (auto display = rmd.acquire())
            {
                logPrintf("LIFE", "burst start: %d s on %dx8 (min hold %d s)",
                          burst_s, width, min_hold_s);
                run_burst(*display, cur.data(), nxt.data(), width,
                          burst_s * 1000, min_hold_s * 1000);
            }
            // no log on contention: display queue is expected to be busy
        }

        task_heartbeat_grace((interval_s + burst_s) * 1000 + 10000);
        for (int s = 0; s < interval_s; s += 5)
            vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
