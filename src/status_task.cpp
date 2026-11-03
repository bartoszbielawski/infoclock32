#include <Arduino.h>
#include <atomic>
#include <ctime>
#include <mutex>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <task_registry.hpp>
#include <time_valid.hpp>
#include <status_task.h>

// Shared state between the triggers (web, MQTT) and the task. The task is the
// only writer of g_running; triggers only post a request and read the info.
static std::mutex             g_mutex;
static Status                 g_pending;          // guarded by g_mutex
static bool                   g_hasPending = false;
static Status                 g_current;          // last started, for status_info()
static std::atomic<bool>      g_active{false};    // requested or showing
static std::atomic<bool>      g_running{false};   // the task holds the display
static std::atomic<bool>      g_cancel{false};
static std::atomic<uint32_t>  g_endMs{0};

static constexpr uint32_t kTickMs           = 250;
static constexpr uint32_t kTimeShowMs       = 3000;
static constexpr uint32_t kAcquireTimeoutMs = 30000;
static constexpr int      kScrollSpeedMs    = 30;

// ── triggers ─────────────────────────────────────────────────────────────────

void status_start(const Status& s)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending    = s;
        g_hasPending = true;
        g_current    = s;
    }
    g_endMs  = millis() + s.durationS * 1000UL;
    g_cancel = false;
    g_active = true;
}

void status_cancel()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_hasPending = false;
    }
    g_cancel = true;
    // Not on screen yet (or already over): nothing for the task to unwind.
    if (!g_running)
        g_active = false;
}

StatusInfo status_info()
{
    StatusInfo info;
    info.active = g_active;
    if (!info.active) return info;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        info.text   = g_current.text;
        info.cue    = g_current.cue;
        info.totalS = g_current.durationS;
    }
    int32_t left = (int32_t)(g_endMs.load() - millis());
    info.remainingS = left > 0 ? (uint32_t)(left / 1000) : 0;
    return info;
}

std::vector<StatusPreset> status_presets()
{
    DataStore& ds = DataStore::getInstance();
    return status_load_presets([&ds](const std::string& key) { return ds.get_value(key, ""); });
}

static void clock_now(bool& valid, int& minute, int& second)
{
    time_t now = time(nullptr);
    valid = time_is_valid(now);
    minute = second = 0;
    if (valid)
    {
        struct tm* t = localtime(&now);
        minute = t->tm_min;
        second = t->tm_sec;
    }
}

StatusError status_request_preset(int slot, const std::string& duration)
{
    for (const StatusPreset& p : status_presets())
    {
        if (p.slot != slot) continue;
        bool valid; int minute, second;
        clock_now(valid, minute, second);
        Status s;
        StatusError e = status_make(p.text, status_cue_name(p.cue),
                                    duration.empty() ? p.duration : duration,
                                    p.timeEveryMin, valid, minute, second, s);
        if (e == StatusError::Ok) status_start(s);
        return e;
    }
    return StatusError::NoSuchPreset;
}

StatusError status_request_custom(const std::string& text, const std::string& cue,
                                  const std::string& duration)
{
    bool valid; int minute, second;
    clock_now(valid, minute, second);
    Status s;
    StatusError e = status_make(text, cue, duration.empty() ? "60" : duration,
                                0, valid, minute, second, s);
    if (e == StatusError::Ok) status_start(s);
    return e;
}

// ── task ─────────────────────────────────────────────────────────────────────

static bool take_request(Status& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_hasPending) return false;
    out = g_pending;
    g_hasPending = false;
    return true;
}

static void draw_time(LMDS& d, int w)
{
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char face[8];
    snprintf(face, sizeof(face), "%02d:%02d", t->tm_hour, t->tm_min);
    int16_t x1, y1; uint16_t tw, th;
    d.clear();
    d.getTextBounds(face, 0, 0, &x1, &y1, &tw, &th);
    d.setCursor((w - (int)tw) / 2, 0);
    d.print(face);
    d.display();
}

// Static text plus the cue for second `sec` of the status.
static void draw_static(LMDS& d, const Status& s, int w, uint32_t sec)
{
    int16_t x1, y1; uint16_t tw, th;
    d.clear();
    d.getTextBounds(s.text.c_str(), 0, 0, &x1, &y1, &tw, &th);
    d.setCursor((w - (int)tw) / 2, 0);
    d.print(s.text.c_str());

    const bool odd = (sec % 2) == 1;
    if (s.cue == StatusCue::Border || (s.cue == StatusCue::Blink && !odd))
    {
        d.setColumn(0, 0xFF);
        d.setColumn(w - 1, 0xFF);
    }
    else if (s.cue == StatusCue::Invert && odd)
    {
        for (int x = 0; x < w; x++)
            for (int y = 0; y < 8; y++)
                d.setPixel(x, y, !d.getPixel(x, y));
    }
    d.display();
}

static int text_width(LMDS& d, const std::string& text)
{
    int16_t x1, y1; uint16_t tw, th;
    d.getTextBounds(text.c_str(), 0, 0, &x1, &y1, &tw, &th);
    return (int)tw;
}

static void run_status(ResourceManager<LMDS>& rmd, Status s)
{
    logPrintf("STS", "start '%s' for %lu s (cue %s)", s.text.c_str(),
              (unsigned long)s.durationS, status_cue_name(s.cue));

    // Priority lane, and ask a running scroll to end now instead of waiting
    // out its minimum slice, so a tap shows up within about a second.
    task_heartbeat_grace(kAcquireTimeoutMs + 10000);
    rmd.requestUrgent(true);
    auto display = rmd.acquire(pdMS_TO_TICKS(kAcquireTimeoutMs), true);
    rmd.requestUrgent(false);
    if (!display)
    {
        logPrintf("STS", "display busy - status not shown");
        g_active = false;
        return;
    }

    LMDS& d = *display;
    const int w = d.getSegments() * 8;
    g_running = true;
    rmd.beginExclusive();

    uint32_t startMs = millis();
    g_endMs = startMs + s.durationS * 1000UL;
    uint32_t nextTimeAtS = s.timeEveryS;
    bool     fits = status_fits(text_width(d, s.text), w, s.cue);
    uint32_t drawnKey = 0xFFFFFFFFu;

    const char* why = "expired";
    while (true)
    {
        task_heartbeat();
        rmd.renewHold();

        Status next;
        if (take_request(next))
        {
            // A new trigger while one is showing replaces it and restarts the timer.
            s = next;
            logPrintf("STS", "replaced by '%s' for %lu s", s.text.c_str(), (unsigned long)s.durationS);
            startMs = millis();
            g_endMs = startMs + s.durationS * 1000UL;
            nextTimeAtS = s.timeEveryS;
            fits = status_fits(text_width(d, s.text), w, s.cue);
            drawnKey = 0xFFFFFFFFu;
        }
        if (g_cancel) { why = "cancelled"; break; }

        const uint32_t elapsedMs = millis() - startMs;
        if (elapsedMs >= s.durationS * 1000UL) break;

        // Everyone else waits on the display for the whole status: keep the
        // watchdog from reading that as a hang.
        task_grace_all(elapsedMs + 90000);

        const uint32_t sec = elapsedMs / 1000;

        // Periodic time, if the preset asks for it and the clock is synced.
        if (s.timeEveryS && sec >= nextTimeAtS && wall_time_valid())
        {
            draw_time(d, w);
            for (uint32_t t = 0; t < kTimeShowMs && !g_cancel; t += kTickMs)
            {
                rmd.renewHold();
                vTaskDelay(kTickMs / portTICK_PERIOD_MS);
            }
            nextTimeAtS = (millis() - startMs) / 1000 + s.timeEveryS;
            drawnKey = 0xFFFFFFFFu;
            continue;
        }

        if (fits)
        {
            // Only blinking/inverting cues change from second to second.
            const uint32_t key = (s.cue == StatusCue::Blink || s.cue == StatusCue::Invert)
                                 ? (sec % 2) : 0;
            if (key != drawnKey)
            {
                draw_static(d, s, w, sec);
                drawnKey = key;
            }
            vTaskDelay(kTickMs / portTICK_PERIOD_MS);
        }
        else
        {
            // Too wide for the matrix: scroll it (safety net, no truncation).
            // One pass can take ~10 s, so cancel reacts at the pass boundary.
            task_heartbeat_grace(60000);
            scrollMessage(s.text, d, kScrollSpeedMs);
            drawnKey = 0xFFFFFFFFu;
        }
    }

    logPrintf("STS", "%s", why);
    // Normal-lane requests that piled up meanwhile are denied (stale), parked
    // priority-lane ones (clock, pushes) are promoted ahead of the release.
    rmd.endExclusive();
    g_running = false;
    g_active  = false;
}

void status_task(void* /*parameter*/)
{
    registerTask("Status", 4096, 120000);
    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        task_heartbeat();
        Status s;
        if (take_request(s))
            run_status(rmd, s);   // guard releases the display on return
        else
            vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}
