// rm_stress.cpp — host stress test for the ResourceManager display handshake.
// Runs on real threads via the FreeRTOS shim, no display hardware needed.
//
//   1. Abandoned-request regression: a requester that times out while its
//      request is still queued must NOT stall the manager once the current
//      holder releases. The pre-fix handshake granted the dead request
//      (the queued entry was never marked abandoned) and then stalled for
//      the full 30 s force-handover timeout waiting for a release that
//      would never come.
//   2. Mutual exclusion under randomized contention: N workers with mixed
//      blocking / short-timeout / priority-lane acquires — at most one
//      holder at any time, and everything must terminate (no deadlock).

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <logger.hpp>
#include <resource_manager.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

// The handshake logic is resource-agnostic — no LMDS/vendor code needed.
struct FakeDisplay {};

static ResourceManager<FakeDisplay>& rm()
{
    return ResourceManager<FakeDisplay>::getInstance();
}

// ── shared invariants ────────────────────────────────────────────────────────

static std::atomic<int>  holderId{-1};   // CAS-guarded: -1 = nobody holds
static std::atomic<int>  violations{0};
static std::atomic<bool> workersStop{false};
static std::atomic<int>  workersDone{0};
static std::atomic<int>  grants{0};
static std::atomic<int>  hookReleases{0};

static int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Called by the manager inside release_access() — proves a holder's release
// passed the current_task check and reached the manager.
static void release_hook(FakeDisplay&)
{
    hookReleases++;
    fprintf(stderr, "[%lld] HOOK release by %s\n",
            (long long)nowMs(), pcTaskGetName(xTaskGetCurrentTaskHandle()));
}

static void fail(const char* what)
{
    fprintf(stderr, "STRESS FAIL: %s\n", what);
    violations++;
}

static void checkExclusive(int id)
{
    int expected = -1;
    if (!holderId.compare_exchange_strong(expected, id))
        fail("mutual exclusion violated");
}

static void releaseExclusive() { holderId = -1; }

// Deterministic per-worker RNG so failures are reproducible.
struct Rng
{
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// ── phase 2: randomized contention workers ───────────────────────────────────

static void worker_task(void* p)
{
    const int  id = (int)(intptr_t)p;
    Rng rng{(uint32_t)id * 7919u + 12345u};

    for (int i = 0; i < 150 && !workersStop; i++)
    {
        bool       priority = (rng.next() % 4) == 0;
        bool       timed    = (rng.next() % 5) == 0;
        TickType_t timeout  = timed ? pdMS_TO_TICKS(5 + rng.next() % 40)
                                    : portMAX_DELAY;

        if (auto display = rm().acquire(timeout, priority))
        {
            grants++;
            fprintf(stderr, "[%lld] W%d grant  iter=%d\n", (long long)nowMs(), id, i);
            checkExclusive(id);
            vTaskDelay(pdMS_TO_TICKS(1 + rng.next() % (timed ? 5 : 15)));
            releaseExclusive();
            fprintf(stderr, "[%lld] W%d done   iter=%d\n", (long long)nowMs(), id, i);
        }
        else if (timed)
        {
            fprintf(stderr, "[%lld] W%d timeout iter=%d\n", (long long)nowMs(), id, i);
        }
        // timed-out / queue-full requests are expected under contention
    }
    workersDone++;
    vTaskDelete(nullptr);
}

// ── phase 1: abandoned request must not stall the manager ────────────────────

static std::atomic<bool>    aHolds{false};
static std::atomic<bool>    aDone{false};
static std::atomic<bool>    bDone{false};
static std::atomic<int64_t> cStartMs{0};
static std::atomic<int64_t> cGrantedMs{-1};

// A: holds the display for 2 s; the manager stays in its release wait the
// whole time, so nothing is popped from the queues until A releases.
static void holder_task(void*)
{
    if (auto display = rm().acquire())
    {
        aHolds = true;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    aDone = true;
    vTaskDelete(nullptr);
}

// B: queues behind A, times out after 150 ms — its request stays queued
// (the manager cannot pop it while waiting for A's release).
static void abandoner_task(void*)
{
    while (!aHolds) vTaskDelay(pdMS_TO_TICKS(5));
    bool got = (bool)rm().acquire(pdMS_TO_TICKS(150));
    if (got)
        fail("timed acquire succeeded while display was held for 2 s");
    bDone = true;
    vTaskDelete(nullptr);
}

// C: queues behind the dead request; must be granted right after A releases.
static void waiter_task(void*)
{
    while (!bDone) vTaskDelay(pdMS_TO_TICKS(5));
    cStartMs = nowMs();
    if (auto display = rm().acquire())
        cGrantedMs = nowMs();
    vTaskDelete(nullptr);
}

int main()
{
    logger_init();

    static FakeDisplay display;
    rm().initialize(&display);
    rm().setPreReleaseHook(release_hook);

    // ── phase 1 ──────────────────────────────────────────────────────────
    xTaskCreate(holder_task,    "stressA", 4096, nullptr, 1, nullptr);
    xTaskCreate(abandoner_task, "stressB", 4096, nullptr, 1, nullptr);
    xTaskCreate(waiter_task,    "stressC", 4096, nullptr, 1, nullptr);

    int64_t deadline = nowMs() + 45000;
    while ((!aDone || !bDone || cGrantedMs < 0) && nowMs() < deadline)
        vTaskDelay(pdMS_TO_TICKS(10));

    if (cGrantedMs < 0)
        fail("waiter never got the display");
    else
    {
        long long waited = (long long)(cGrantedMs - cStartMs);
        fprintf(stderr, "[rm_stress] abandoned request: waiter granted after %lld ms\n", waited);
        if (waited > 4000)
            fail("manager stalled on an abandoned request (>4 s)");
    }

    // ── phase 2 ──────────────────────────────────────────────────────────
    constexpr int kWorkers = 6;
    char names[kWorkers][16];
    for (int i = 0; i < kWorkers; i++)
    {
        snprintf(names[i], sizeof(names[i]), "stressW%d", i + 1);
        xTaskCreate(worker_task, names[i], 4096, (void*)(intptr_t)(i + 1), 1, nullptr);
    }

    deadline = nowMs() + 120000;
    while (workersDone < kWorkers && nowMs() < deadline)
        vTaskDelay(pdMS_TO_TICKS(50));
    if (workersDone < kWorkers)
        fail("workers did not finish within 120 s (deadlock?)");

    fprintf(stderr, "[rm_stress] contention: %d workers finished, %d violations, "
                    "%d grants, %d hook releases\n",
            workersDone.load(), violations.load(), grants.load(), hookReleases.load());

    if (violations == 0)
    {
        fprintf(stderr, "all resource manager stress checks passed\n");
        return 0;
    }
    return 1;
}
