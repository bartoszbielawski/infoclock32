#include <Arduino.h>

#include <config_warnings.hpp>
#include <data_store.hpp>
#include <display_strings.hpp>
#include <logger.hpp>
#include <night_mode_task.h>
#include <push_display.h>

std::vector<DeprecatedUse> config_warnings()
{
    return find_deprecated(dataStoreView());
}

void config_warnings_log()
{
    for (const auto& w : config_warnings())
        logPrintf("SYS", "deprecated config: %s - %s", w.what.c_str(), w.advice.c_str());
}

void config_warning_tick()
{
    // First reminder ~10 min after boot, so it does not collide with the IP
    // scroll and the first weather fetch.
    static uint32_t nextMs = 10UL * 60000UL;

    const uint32_t now = millis();
    if ((int32_t)(now - nextMs) < 0) return;

    auto& ds = DataStore::getInstance();
    int minutes = ds.get_value<int>("config_warn_interval_min", 60);
    if (minutes < 0) minutes = 0;
    if (minutes > 1440) minutes = 1440;
    if (minutes == 0) { nextMs = now + 600000UL; return; }   // off: look again later in case it is switched on

    // The display is blanked at night: try again in a minute instead of
    // burning the whole interval.
    if (night_mode_active()) { nextMs = now + 60000UL; return; }

    nextMs = now + (uint32_t)minutes * 60000UL;
    if (config_warnings().empty()) return;
    show_once(display_strings(parse_lang(ds.get_value("language", "en"))).check_config, 30);
}
