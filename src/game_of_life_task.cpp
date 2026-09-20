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
static constexpr int DEFAULT_FUEL_PCT   = 18;   // random cells added to an image seed

// How long the untouched screen contents stay up before the fuel is added
// and evolution starts — long enough to read the time you just saw.
static constexpr int SEED_REVEAL_MS     = 900;

// Below this many lit pixels there is no image worth growing from (a blank
// screen just after a wipe, say), so the burst falls back to random soup.
static constexpr int MIN_SEED_POP       = 12;

static int rnd100() { return (int)random(0, 100); }

static void seed_field(uint8_t* cells, size_t size)
{
    for (size_t i = 0; i < size; i++)
        cells[i] = (rnd100() < 45) ? 1 : 0;
}

// Copies whatever is on the matrix right now into the board. The display is
// never cleared between holds, so this is the clock, the tail of the last
// message, or whatever the previous task left behind. Returns the population
// so the caller can tell an image from an empty screen.
static int snapshot_display(LMDS& display, uint8_t* cells, int width)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < width; x++)
            cells[y * width + x] = display.getPixel(x, y) ? 1 : 0;
    return life_population(cells, (size_t)width * 8);
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

// Per-burst settings, read from the config once per cycle.
struct BurstSettings
{
    int      burst_ms;
    uint32_t min_hold_ms;
    bool     seed_from_display;
    int      fuel_pct;
};

// Runs one animation burst on an already-acquired display, then returns.
// Resumes `board` where the last burst left off unless it was reseeded; ends
// early when the board settles or when the hold may be cut short.
static BurstEnd run_burst(LMDS& display, LifeBoard& board, int width,
                          const BurstSettings& cfg)
{
    const size_t size = (size_t)width * 8;
    uint8_t* cur = board.cur.data();
    uint8_t* nxt = board.nxt.data();

    if (!board.live)
    {
        // Grow out of the display rather than appearing from nowhere: take
        // what is on screen, leave it up long enough to be read, then add
        // fuel so the thin strokes have something to react with.
        int pop = cfg.seed_from_display ? snapshot_display(display, cur, width) : 0;
        if (pop >= MIN_SEED_POP)
        {
            logPrintf("LIFE", "seeded from the display: %d cells + %d%% fuel",
                      pop, cfg.fuel_pct);
            vTaskDelay(SEED_REVEAL_MS / portTICK_PERIOD_MS);
            life_add_fuel(cur, size, cfg.fuel_pct, rnd100);
        }
        else
        {
            if (cfg.seed_from_display)
                logPrintf("LIFE", "screen too empty (%d cells) — random soup", pop);
            seed_field(cur, size);
        }
        board.cycle.reset();
        board.cycle.observe(cur, size);
        board.generation = 0;
        board.live = true;
    }

    DisplayHold<LMDS> hold(ResourceManager<LMDS>::getInstance(), cfg.min_hold_ms);
    while (hold.elapsed() < (uint32_t)cfg.burst_ms)
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
                    logPrintf("LIFE", "burst start: new board, %d s on %dx8 (min hold %d s)",
                              burst_s, width, min_hold_s);
                BurstSettings cfg;
                cfg.burst_ms          = burst_s * 1000;
                cfg.min_hold_ms       = (uint32_t)min_hold_s * 1000;
                cfg.seed_from_display = ds.get_value<int>("life_seed_display", 1) != 0;
                cfg.fuel_pct          = max(0, min(50, ds.get_value<int>("life_fuel_pct",
                                                                        DEFAULT_FUEL_PCT)));
                run_burst(*display, board, width, cfg);
            }
            // no log on contention: display queue is expected to be busy
        }

        task_heartbeat_grace((interval_s + burst_s) * 1000 + 10000);
        for (int s = 0; s < interval_s; s += 5)
            vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
