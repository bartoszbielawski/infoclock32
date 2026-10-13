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

void sensor_task(void* parameter)
{
    registerTask("Sensor", 4096, 60000);
    std::vector<Sensor*>* list = static_cast<std::vector<Sensor*>*>(parameter);
    const size_t N = list->size();

    if (N == 0)
    {
        logPrintf("TMP", "no sensors configured (sensors=?) — task exiting");
        sensorListExit(list);
        vTaskDelete(nullptr);
        return;
    }

    // Initialise every sensor up front. A single bad bus read (e.g. a missing
    // I2C chip) must not take the whole round down — begin() failures log and
    // continue; only if all sensors fail do we give up.
    size_t okCount = 0;
    for (size_t i = 0; i < N; i++)
    {
        if (list->at(i)->begin())
            okCount++;
        else
            logPrintf("TMP", "sensor '%s' failed to initialize — will be skipped in the round",
                      list->at(i)->name());
    }
    logPrintf("TMP", "starting with %zu sensor(s)", okCount);
    if (okCount == 0)
    {
        logPrintf("TMP", "all sensors failed to initialize, task exiting");
        sensorListExit(list);
        vTaskDelete(nullptr);
        return;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    time_t last_update = 0;
    time_t last_display = 0;
    size_t rr_index = 0;

    float temp = 0.0f;
    float pressure = 0.0f;
    float humidity = 0.0f;
    int co2 = 0;

    // Pressure trend (valid only when a sensor in the round provides pressure)
    bool haveTrend = false;
    TrendKind trendKind = TREND_STEADY;
    float trendRate = 0.0f;

    bool read_success = false;

    // Capability union across the whole round: a display item is shown if ANY
    // sensor provides it, so the filter is computed once here.
    bool anyHumidity = false, anyPressure = false, anyCo2 = false;
    for (size_t i = 0; i < N; i++)
    {
        anyHumidity  = anyHumidity  || list->at(i)->hasHumidity();
        anyPressure  = anyPressure  || list->at(i)->hasPressure();
        anyCo2       = anyCo2       || list->at(i)->hasCo2();
    }

    // Complaints about requested-but-unavailable measurements: once per item
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

        time_t now = time(nullptr);

        // Round-robin: refresh one sensor per interval. The index always
        // advances so a dead sensor doesn't stall the round.
        if (difftime(now, last_update) >= interval_s)
        {
            Sensor* s = list->at(rr_index);
            rr_index = (rr_index + 1) % N;

            read_success = s->read();
            if (read_success)
            {
                last_update = now;
                auto& rs = RuntimeStore::getInstance();

                temp = s->temperature() + tempOffset;
                rs.set("temp_c", temp, runtimeTempFormat());

                if (s->hasPressure())
                {
                    pressure = s->pressure() + pressOffset;
                    rs.set("temp_hpa", pressure, "%.0f");

                    // Feed the trend history and refresh the trend placeholders.
                    // The trend is computed from the un-offset reading so the
                    // calibration offset doesn't shift the slope.
                    float rawPressure = s->pressure();
                    PressureHistoryStore::getInstance().add(now, rawPressure);
                    haveTrend = PressureHistoryStore::getInstance().trend(now, trendKind, trendRate);
                    if (haveTrend)
                    {
                        rs.set("pressure_trend", pressureTrendName(trendKind));
                        rs.set("pressure_rate", trendRate, "%+.2f");
                    }
                }

                if (s->hasHumidity())
                {
                    humidity = s->humidity() + rhOffset;
                    rs.set("temp_rh", humidity, "%.0f");
                }

                if (s->hasCo2())
                {
                    co2 = (int)(s->co2Concentration() + co2Offset);
                    rs.set("temp_co2", co2, "%d");
                }
            }
        }

        if (not read_success)
        {
            logPrintf("TMP", "failed to read from sensor (round index %zu)", rr_index);
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        bool showDue = !items.empty() &&
                       (show_interval_s == 0 || difftime(now, last_display) >= show_interval_s);
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
                last_display = time(nullptr);
            }
        }

        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}