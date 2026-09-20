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
#include <graphic_utils.hpp>
#include <life.hpp>
#include <night_utils.hpp>

static constexpr int DEFAULT_INTERVAL_S = 300;
static constexpr int DEFAULT_BURST_S    = 30;
static constexpr int GEN_DELAY_MS       = 80;   // ~12 generations per second

static void seed_field(uint8_t* cells, size_t size)
{
    for (size_t i = 0; i < size; i++)
        cells[i] = (random(0, 100) < 45) ? 1 : 0;
}

// Why a burst stopped. Only a settled board is thrown away; the other two
// mean the universe was still evolving when the display had to go back.
enum class BurstEnd { Settled, Preempted, Expired };

// The universe, kept alive between bursts. The buffers were always reused —
// what persists now is the pattern in them, plus the detector history and
// generation count that belong with it.
struct LifeBoard
{
    std::vector<uint8_t> cur, nxt;
    LifeCycleDetector    cycle;
    int                  generation = 0;
    bool                 live       = false;   // false: seed fresh soup next burst
};

// Runs one animation burst on an already-acquired display, then returns.
// Resumes `board` where the last burst left off unless it was reseeded; ends
// early when the board settles or when the hold may be cut short.
static BurstEnd run_burst(LMDS& display, LifeBoard& board, int width,
                          int burst_ms, uint32_t min_hold_ms)
{
    const size_t size = (size_t)width * 8;
    uint8_t* cur = board.cur.data();
    uint8_t* nxt = board.nxt.data();

    if (!board.live)
    {
        seed_field(cur, size);
        board.cycle.reset();
        board.cycle.observe(cur, size);
        board.generation = 0;
        board.live = true;
    }

    DisplayHold<LMDS> hold(ResourceManager<LMDS>::getInstance(), min_hold_ms);
    while (hold.elapsed() < (uint32_t)burst_ms)
    {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < width; x++)
                display.setPixel(x, y, cur[y * width + x] != 0);
        display.display();

        // Filler content: hand the display back as soon as the shared
        // minimum-slice policy allows it. The animation can cut off anywhere —
        // the board stays put and the next burst picks it up here.
        if (!hold.keepGoing())
        {
            logPrintf("LIFE", "burst preempted after %lu ms at gen %d",
                      (unsigned long)hold.elapsed(), board.generation);
            return BurstEnd::Preempted;
        }

        int pop = life_step(cur, nxt, width);
        std::memcpy(cur, nxt, size);
        board.generation++;

        // A board that died back, or one that repeats a state it already
        // showed (still life, blinker, any oscillator up to the detector's
        // window), has nothing left to show — end the burst and drop it, so
        // the next one starts from fresh soup instead of a frozen pattern.
        int period = board.cycle.observe(cur, size);
        if (life_is_settled(pop, period))
        {
            logPrintf("LIFE", "settled after %d gens (pop %d, period %d) — ending burst",
                      board.generation, pop, period);
            board.live = false;
            return BurstEnd::Settled;
        }

        vTaskDelay(GEN_DELAY_MS / portTICK_PERIOD_MS);
    }

    logPrintf("LIFE", "burst time up at gen %d — resuming next time", board.generation);
    return BurstEnd::Expired;
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

    LifeBoard board;

    while (true)
    {
        task_heartbeat();
        int interval_s = max(30, min(3600, ds.get_value<int>("life_interval_s", DEFAULT_INTERVAL_S)));
        int burst_s    = max(5,  min(120, ds.get_value<int>("life_burst_s",    DEFAULT_BURST_S)));
        // Guaranteed slice before the clock can preempt the burst. Defaults to
        // the shared display policy (display_min_hold_s); life_min_hold_s
        // overrides it for bursts only. Capped at burst_s; 0 yields at once.
        int shared_s   = (int)(display_min_hold_ms() / 1000);
        int min_hold_s = max(0, min(burst_s, ds.get_value<int>("life_min_hold_s", shared_s)));

        int width = rmd.getResourceRef().getSegments() * 8;
        if ((size_t)width * 8 != board.cur.size())
        {
            board.cur.resize((size_t)width * 8);
            board.nxt.resize((size_t)width * 8);
            board.live = false;          // a resized board cannot be resumed
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
                if (board.live)
                    logPrintf("LIFE", "burst start: resuming at gen %d (%d s, min hold %d s)",
                              board.generation, burst_s, min_hold_s);
                else
                    logPrintf("LIFE", "burst start: fresh soup, %d s on %dx8 (min hold %d s)",
                              burst_s, width, min_hold_s);
                run_burst(*display, board, width,
                          burst_s * 1000, (uint32_t)min_hold_s * 1000);
            }
            // no log on contention: display queue is expected to be busy
        }

        task_heartbeat_grace((interval_s + burst_s) * 1000 + 10000);
        for (int s = 0; s < interval_s; s += 5)
            vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
