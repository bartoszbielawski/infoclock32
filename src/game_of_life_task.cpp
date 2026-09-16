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
static constexpr int DEFAULT_BURST_S    = 15;
static constexpr int GEN_DELAY_MS       = 80;   // ~12 generations per second

static void seed_field(uint8_t* cells, size_t size)
{
    for (size_t i = 0; i < size; i++)
        cells[i] = (random(0, 100) < 45) ? 1 : 0;
}

// Runs one animation burst on an already-acquired display, then returns.
static void run_burst(LMDS& display, uint8_t* cur, uint8_t* nxt,
                      int width, int burst_ms)
{
    const size_t size = (size_t)width * 8;
    seed_field(cur, size);

    unsigned long start = millis();
    while ((long)(millis() - start) < burst_ms)
    {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < width; x++)
                display.setPixel(x, y, cur[y * width + x] != 0);
        display.display();

        int pop = life_step(cur, nxt, width);
        bool stagnant = (std::memcmp(cur, nxt, size) == 0);
        std::memcpy(cur, nxt, size);

        if (pop < 3 || stagnant)
            seed_field(cur, size);   // collapse or still-life: fresh soup

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
                logPrintf("LIFE", "burst start: %d s on %dx8", burst_s, width);
                run_burst(*display, cur.data(), nxt.data(), width, burst_s * 1000);
            }
            // no log on contention: display queue is expected to be busy
        }

        task_heartbeat_grace((interval_s + burst_s) * 1000 + 10000);
        for (int s = 0; s < interval_s; s += 5)
            vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
