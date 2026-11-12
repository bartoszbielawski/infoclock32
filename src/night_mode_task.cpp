#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <task_registry.hpp>
#include <freertos/task.h>

#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <night_utils.hpp>
#include <time_valid.hpp>
#include <auto_brightness.hpp>
#include <tick_utils.hpp>
#include <runtime_store.hpp>
#include <night_mode_task.h>

#include <esp_timer.h>
#include <atomic>
#include <string>

static std::atomic<bool> g_night{false};
static std::atomic<int>  g_level{7};

// Auto-brightness inputs. 32-bit atomics only (no 64-bit libatomic on the C3).
static constexpr int kLuxStaleS = 180;    // a long scroll in the sensor task must not drop it
static std::atomic<int> g_luxCenti{0};    // lux * 100
static std::atomic<int> g_luxStamp{-1};   // monotonic seconds of the last report, -1 = none
static std::atomic<int> g_manualUntil{0}; // monotonic seconds; auto paused until then

static int monoS() { return (int)(esp_timer_get_time() / 1000000LL); }

void report_ambient_lux(float lux)
{
    if (!(lux >= 0.0f)) return;
    if (lux > 20000000.0f) lux = 20000000.0f;
    g_luxCenti = (int)(lux * 100.0f);
    g_luxStamp = monoS();
}

static bool lux_fresh()
{
    int stamp = g_luxStamp;
    return stamp >= 0 && (monoS() - stamp) <= kLuxStaleS;
}

bool auto_brightness_active()
{
    return DataStore::getInstance().get_value<int>("auto_brightness", 0) != 0 && lux_fresh();
}

bool night_mode_active() { return g_night; }
int  current_display_brightness() { return g_level; }

void apply_display_brightness(int level)
{
    level = max(0, min(15, level));
    g_level = level;
    // Applied by whoever holds the display on their next frame (see LMDS),
    // so this never waits for the display.
    ResourceManager<LMDS>::getInstance().getResourceRef().requestIntensity((uint8_t)level);
    RuntimeStore::getInstance().set("display_brightness", std::to_string(level));
}

// Deferred config save for brightness changes (see set_user_brightness).
static portMUX_TYPE s_saveMux = portMUX_INITIALIZER_UNLOCKED;
static DeferredAction s_brightnessSave;
static constexpr int kBrightnessSaveDelayS = 5;

static void requestBrightnessSave()
{
    const int64_t now = monoS();
    taskENTER_CRITICAL(&s_saveMux);
    s_brightnessSave.touch(now, kBrightnessSaveDelayS);
    taskEXIT_CRITICAL(&s_saveMux);
}

static bool brightnessSaveDue()
{
    const int64_t now = monoS();
    taskENTER_CRITICAL(&s_saveMux);
    bool due = s_brightnessSave.takeDue(now);
    taskEXIT_CRITICAL(&s_saveMux);
    return due;
}

bool set_user_brightness(int level)
{
    level = max(0, min(15, level));
    apply_display_brightness(level);
    // A manual change pauses auto-brightness for a while so it isn't undone
    // within a second.
    g_manualUntil = monoS() + max(0, DataStore::getInstance().get_value<int>("ab_pause_s", 600));
    // At night this is a temporary override: the configured daytime level
    // must survive, so the next night → day switch restores it.
    if (g_night)
        return false;
    DataStore::getInstance().set_value("brightness", std::to_string(level));
    // Saved by the night-mode task a few seconds after the last change: a
    // Home Assistant slider sends many steps, each used to rewrite the file.
    requestBrightnessSave();
    return true;
}

static AutoBrightnessParams auto_params()
{
    DataStore& ds = DataStore::getInstance();
    AutoBrightnessParams p;
    p.luxMax   = ds.get_value("ab_lux_max", p.luxMax);
    p.gamma    = ds.get_value("ab_gamma",   p.gamma);
    p.minLevel = ds.get_value("ab_min",     p.minLevel);
    p.maxLevel = ds.get_value("ab_max",     p.maxLevel);
    p.tauS     = ds.get_value("ab_tau_s",   p.tauS);
    return sanitize(p);
}

// Level the configured schedule asks for right now.
static int scheduled_level()
{
    DataStore& ds = DataStore::getInstance();
    return g_night ? ds.get_value("night_brightness", 1)
                   : ds.get_value("brightness",        7);
}

// Housekeeping calls night_mode_tick() every second; it starts acting 15 s
// after night_mode_start() (let DataStore load and NTP sync first).
static int s_firstTickS = -1;

void night_mode_start()
{
    s_firstTickS = monoS() + 15;
}

void night_mode_tick()
{
    static bool last_was_night = false;
    static bool was_auto = false;
    static int  next_night_check = 0;
    static AutoBrightness autob;

    if (s_firstTickS < 0 || monoS() < s_firstTickS)
        return;

    DataStore& ds = DataStore::getInstance();
    const int now_s = monoS();
    const bool auto_on = auto_brightness_active();

    if (now_s >= next_night_check)
    {
        next_night_check = now_s + 60;

        int start_min = parse_hhmm(ds.get_value("night_start", ""));
        int end_min   = parse_hhmm(ds.get_value("night_end",   ""));

        if (!wall_time_valid())
        {
            // Unsynced clock: keep the current brightness, re-check in a minute.
        }
        else if (start_min < 0 || end_min < 0)
        {
            // Night mode not configured — reset state.
            last_was_night = false;
            g_night = false;
        }
        else
        {
            time_t now = time(nullptr);
            struct tm tBuf;
            struct tm* t = localtime_r(&now, &tBuf);
            int now_min = t->tm_hour * 60 + t->tm_min;

            bool is_night = in_night_window(now_min, start_min, end_min);

            if (is_night != last_was_night)
            {
                last_was_night = is_night;
                g_night = is_night;
                if (auto_on)
                {
                    logPrintf("NGT", "night mode %s (brightness left to auto)",
                              is_night ? "on" : "off");
                }
                else
                {
                    int level = scheduled_level();
                    apply_display_brightness(level);
                    logPrintf("NGT", "night mode %s → brightness %d",
                              is_night ? "on" : "off", level);
                }
            }
        }
    }

    const bool paused = now_s < g_manualUntil;

    if (was_auto && !auto_on && !paused)
    {
        // The light sensor went quiet (or auto was switched off): back to
        // the schedule.
        int level = scheduled_level();
        apply_display_brightness(level);
        logPrintf("NGT", "auto-brightness inactive → brightness %d", level);
    }
    if (!was_auto && auto_on)
        logPrintf("NGT", "auto-brightness active");
    was_auto = auto_on;

    if (auto_on)
    {
        if (!paused)
        {
            int cur  = g_level;
            int next = autob.update(now_s, g_luxCenti / 100.0f, cur, auto_params());
            if (next != cur)
            {
                apply_display_brightness(next);
                logPrintf("NGT", "auto-brightness %.1f lx → %d", g_luxCenti / 100.0f, next);
            }
        }
    }
    else
    {
        autob.reset();
    }

    if (brightnessSaveDue())
        DataStore::getInstance().save_to_file("/config.txt");
}

// Stand-alone loop, used by the host prototype; the device runs the ticks
// from the housekeeping task.
void night_mode_task(void* /*parameter*/)
{
    registerTask("NightMode", 4096, 120000);
    night_mode_start();
    for (;;)
    {
        task_heartbeat();
        night_mode_tick();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
