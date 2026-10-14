#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <sensor.hpp>
#include <pressure_history.hpp>
#include <temp_display.hpp>
#include <sensor_merge.hpp>
#include <esp_timer.h>
#include <string>
#include <vector>

// Default poll interval in seconds (per sensor). With N sensors the full
// round takes N * interval.
static constexpr int DEFAULT_INTERVAL_S = 300;

// Config keys (multi-sensor rework: the old temp_* display/poll keys are gone;
// see README "Sensors" for the new surface).
static const char CFG_INTERVAL[]      = "sensor_interval";
static const char CFG_TEMP_OFFSET[]   = "temp_offset";
static const char CFG_RH_OFFSET[]     = "humidity_offset";
static const char CFG_PRESS_OFFSET[]  = "pressure_offset";
static const char CFG_CO2_OFFSET[]    = "co2_offset";
static const char CFG_DISPLAY[]       = "sensor_display";
static const char CFG_SHOW_INTERVAL[] = "sensor_readout_interval";
static const char CFG_HOLD[]          = "sensor_hold_s";

// The task parameter is a heap-allocated std::vector<Sensor*> built by
// createSensorList() in main(). The task owns it and deletes every sensor on
// exit (both the normal exit and the init-failure exit).
static void sensorListExit(std::vector<Sensor*>* list)
{
    for (Sensor* s : *list) delete s;
    delete list;
    task_mark_exited();   // stop watchdog supervision before leaving the task
}

// Monotonic seconds since boot. Used for polling cadence and staleness so an
// NTP step can't make a reading look ancient (or delay the first poll while
// the wall clock is still near 1970). Wall time is kept only for the pressure
// history.
static int64_t monoSeconds()
{
    return esp_timer_get_time() / 1000000LL;
}

static const char* const kMeasurementKey[MEAS_COUNT] =
    { "temp_c", "temp_rh", "temp_hpa", "temp_co2" };

void sensor_task(void* parameter)
{
    registerTask("Sensor", 4096, 60000);
    std::vector<Sensor*>* list = static_cast<std::vector<Sensor*>*>(parameter);

    // Initialise every sensor up front. A single bad bus read (e.g. a missing
    // I2C chip) must not take the whole round down: sensors whose begin()
    // fails are dropped from the round (not polled), and stubs are dropped as
    // soon as a real sensor is present. If nothing is left, the task exits.
    std::vector<bool> initOk;
    for (size_t i = 0; i < list->size(); i++)
    {
        bool ok = list->at(i)->begin();
        initOk.push_back(ok);
        if (!ok)
            logPrintf("TMP", "sensor '%s' failed to initialize — dropped from the round",
                      list->at(i)->name());
    }
    pruneSensors(*list, initOk);

    const size_t N = list->size();
    if (N == 0)
    {
        logPrintf("TMP", "no usable sensors (sensors=? stub/failed) — task exiting");
        sensorListExit(list);
        vTaskDelete(nullptr);
        return;
    }
    logPrintf("TMP", "starting with %zu sensor(s)", N);

    auto& rmd = ResourceManager<LMDS>::getInstance();
    size_t rr_index = 0;

    // Monotonic schedule (seconds). 0 => first poll happens immediately.
    int64_t nextPollAt = 0;
    int64_t lastDisplay = -1;

    float temp = 0.0f;
    float pressure = 0.0f;
    float humidity = 0.0f;
    int co2 = 0;

    // One slot per measurement: who wrote it and when (see sensor_merge.hpp).
    MeasurementSlot slots[MEAS_COUNT];
    int owner[MEAS_COUNT];
    for (int m = 0; m < MEAS_COUNT; m++)
        owner[m] = firstProvider(*list, (Measurement)m);

    // Pressure trend (valid only while a pressure reading is fresh)
    bool haveTrend = false;
    TrendKind trendKind = TREND_STEADY;
    float trendRate = 0.0f;

    // Capability union across the round, used only to decide whether a
    // requested display item can ever be shown (log once per item).
    bool anyHumidity = owner[MEAS_HUMIDITY] >= 0;
    bool anyPressure = owner[MEAS_PRESSURE] >= 0;
    bool anyCo2      = owner[MEAS_CO2] >= 0;

    bool unavailableLogged[TDI_COUNT] = {};

    while (true)
    {
        task_heartbeat();

        int interval_s = DataStore::getInstance().get_value(CFG_INTERVAL, DEFAULT_INTERVAL_S);
        if (interval_s < 5) interval_s = DEFAULT_INTERVAL_S;

        // Calibration offsets applied at write time, per measurement.
        float tempOffset    = DataStore::getInstance().get_value(CFG_TEMP_OFFSET, 0.0f);
        float rhOffset      = DataStore::getInstance().get_value(CFG_RH_OFFSET, 0.0f);
        float pressOffset   = DataStore::getInstance().get_value(CFG_PRESS_OFFSET, 0.0f);
        float co2Offset     = DataStore::getInstance().get_value(CFG_CO2_OFFSET, 0.0f);

        // Display cadence, decoupled from the poll interval: 0 = every wake
        int show_interval_s = DataStore::getInstance().get_value<int>(CFG_SHOW_INTERVAL, 0);
        if (show_interval_s < 0) show_interval_s = 0;

        // How long each reading stays on screen: 0 = the historical ~2 s flash
        int hold_s = DataStore::getInstance().get_value<int>(CFG_HOLD, 0);
        if (hold_s < 0) hold_s = 0;
        if (hold_s > 30) hold_s = 30;
        int holdMs = hold_s * 1000;

        // What to show, in what order; empty list = never scroll
        std::vector<TempDisplayItem> items = parseDisplayList(
            DataStore::getInstance().get_value(CFG_DISPLAY, kTempDisplayDefault));

        // Drop measurements no sensor in the round can provide (log once per item)
        for (auto it = items.begin(); it != items.end();)
        {
            if (!itemAvailable(*it, anyHumidity, anyPressure, anyCo2))
            {
                if (!unavailableLogged[*it])
                {
                    unavailableLogged[*it] = true;
                    logPrintf("TMP", "'%s' requested but no configured sensor provides it",
                              tempDisplayName(*it));
                }
                it = items.erase(it);
            }
            else
            {
                ++it;
            }
        }

        int64_t now = monoSeconds();
        bool newData = false;   // a poll succeeded in this wake
        const int64_t staleAfter = staleAfterSeconds(N, interval_s);

        // Round-robin: refresh one sensor per interval. The index always
        // advances so a dead sensor doesn't stall the round.
        if (now >= nextPollAt)
        {
            const size_t idx = rr_index;
            Sensor* s = list->at(idx);
            rr_index = (rr_index + 1) % N;

            if (s->read())
            {
                nextPollAt = now + interval_s;
                newData = true;
                auto& rs = RuntimeStore::getInstance();
                time_t wall = time(nullptr);

                auto accepts = [&](Measurement m) {
                    return sensorProvides(*s, m) &&
                           slotAccepts(slots[m], (int)idx, owner[m], now, staleAfter);
                };
                auto stamp = [&](Measurement m) {
                    slots[m].have = true;
                    slots[m].source = (int)idx;
                    slots[m].stamp = now;
                };

                if (accepts(MEAS_TEMP))
                {
                    temp = s->temperature() + tempOffset;
                    rs.set("temp_c", temp, runtimeTempFormat());
                    stamp(MEAS_TEMP);
                }

                if (accepts(MEAS_PRESSURE))
                {
                    pressure = s->pressure() + pressOffset;
                    rs.set("temp_hpa", pressure, "%.0f");
                    stamp(MEAS_PRESSURE);

                    // Feed the trend history and refresh the trend placeholders.
                    // The trend is computed from the un-offset reading so the
                    // calibration offset doesn't shift the slope.
                    PressureHistoryStore::getInstance().add(wall, s->pressure());
                    haveTrend = PressureHistoryStore::getInstance().trend(wall, trendKind, trendRate);
                    if (haveTrend)
                    {
                        rs.set("pressure_trend", pressureTrendName(trendKind));
                        rs.set("pressure_rate", trendRate, "%+.2f");
                    }
                }

                if (accepts(MEAS_HUMIDITY))
                {
                    humidity = s->humidity() + rhOffset;
                    rs.set("temp_rh", humidity, "%.0f");
                    stamp(MEAS_HUMIDITY);
                }

                if (accepts(MEAS_CO2))
                {
                    int v = (int)(s->co2Concentration() + co2Offset);
                    co2 = v < 0 ? 0 : v;
                    rs.set("temp_co2", co2, "%d");
                    stamp(MEAS_CO2);
                }
            }
            else
            {
                logPrintf("TMP", "failed to read from sensor '%s' (round index %zu)",
                          s->name(), idx);
                // Retry the next sensor sooner than a full interval.
                nextPollAt = now + (interval_s < 30 ? interval_s : 30);
            }
        }

        // Drop readings whose provider has gone quiet: the placeholders and
        // MQTT state disappear instead of repeating the last value forever.
        {
            auto& rs = RuntimeStore::getInstance();
            for (int m = 0; m < MEAS_COUNT; m++)
            {
                if (!slotStale(slots[m], now, staleAfter)) continue;
                slots[m].have = false;
                logPrintf("TMP", "%s stale (no update for %lld s) — dropped",
                          kMeasurementKey[m], (long long)(now - slots[m].stamp));
                rs.remove(kMeasurementKey[m]);
                if (m == MEAS_PRESSURE)
                {
                    haveTrend = false;
                    rs.remove("pressure_trend");
                    rs.remove("pressure_rate");
                }
            }
        }

        // Scroll only measurements that currently have a fresh value.
        for (auto it = items.begin(); it != items.end();)
        {
            bool fresh = true;
            switch (*it)
            {
                case TDI_TEMP:     fresh = slots[MEAS_TEMP].have;     break;
                case TDI_HUMIDITY: fresh = slots[MEAS_HUMIDITY].have; break;
                case TDI_PRESSURE: fresh = slots[MEAS_PRESSURE].have; break;
                case TDI_CO2:      fresh = slots[MEAS_CO2].have;      break;
                default: break;
            }
            it = fresh ? it + 1 : items.erase(it);
        }

        // show_interval 0 = once per successful poll; otherwise time-gated.
        bool showDue = !items.empty() &&
                       (show_interval_s == 0
                            ? newData
                            : (lastDisplay < 0 || (now - lastDisplay) >= show_interval_s));
        if (showDue)
        {
            // Blocking acquire: cover the worst-case wait + scroll holds, then
            // re-anchor the watchdog window once granted (clock-task pattern).
            task_heartbeat_grace(90000);
            if (auto display = rmd.acquire())
            {
                task_heartbeat();
                task_heartbeat_grace(30000);
                for (size_t i = 0; i < items.size(); i++)
                {
                    switch (items[i])
                    {
                        case TDI_TEMP:
                            showMessage(formatTempLine(temp), display, holdMs);
                            break;
                        case TDI_HUMIDITY:
                            showMessage(formatHumidityLine(humidity), display, holdMs);
                            break;
                        case TDI_PRESSURE:
                        {
                            const uint8_t* trendIcon = haveTrend ? trendIconFor(trendKind) : nullptr;
                            showMessage(trendIcon, kWeatherIconWidth,
                                        formatPressureLine(pressure), display, holdMs);
                            break;
                        }
                        case TDI_CO2:
                            showMessage(formatCo2Line(co2), display, holdMs);
                            break;
                        default:
                            break;
                    }
                }
                lastDisplay = monoSeconds();
            }
        }

        // Sleep until the next poll or scroll is due (capped at 30 s so the
        // watchdog heartbeat and config changes are picked up promptly).
        now = monoSeconds();
        int64_t untilShow = (show_interval_s > 0 && lastDisplay >= 0)
                                ? (lastDisplay + show_interval_s - now) : -1;
        vTaskDelay(wakeDelayMs(nextPollAt - now, untilShow) / portTICK_PERIOD_MS);
    }
}
