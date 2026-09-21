#include <Arduino.h>
#include <pgmspace.h>
#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <custom_message.hpp>
#include <string>
#include <vector>
#include <ctime>

static constexpr int DEFAULT_CYCLE_S = 60;

static std::vector<CustomMessage> load_messages()
{
    std::vector<CustomMessage> out;
    auto& ds = DataStore::getInstance();

    // Collect all keys of the form "message_<name>_text"
    auto keys = ds.get_keys_with_prefix("message_");
    for (const auto& key : keys)
    {
        std::string name;
        if (!parse_message_key(key, name)) continue;

        std::string text = ds.get_value(key, "");
        if (text.empty()) continue;

        CustomMessage m;
        m.text      = text;
        m.start     = parse_date(ds.get_value("message_" + name + "_start",     ""));
        m.end       = parse_date(ds.get_value("message_" + name + "_end",       ""));
        m.countdown = parse_date(ds.get_value("message_" + name + "_countdown", ""));
        out.push_back(m);
    }
    return out;
}

void custom_message_task(void* /*parameter*/)
{
    registerTask("CustomMsg", 4096, 90000);
    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        task_heartbeat();
        auto messages = load_messages();
        time_t now    = time(nullptr);

        int cycle_s = max(10, min(3600,
            DataStore::getInstance().get_value<int>("msg_interval", DEFAULT_CYCLE_S)));

        task_heartbeat_grace(messages.size() * 12000 + 12000);  // each scroll ~7 s
        for (const auto& m : messages)
        {
            // start is inclusive (>= midnight of start day)
            if (m.start >= 0 && now < m.start) continue;
            // end is inclusive — show through end of the end day (DST-aware)
            if (m.end   >= 0 && now >= day_end(m.end)) continue;
            // countdowns auto-hide from the day after the target unless end overrides
            if (is_expired(m, now)) continue;

            std::string display = build_display(m, now);
            logPrintf("MSG", "%s", display.c_str());

            if (auto d = rmd.acquire())
                scrollMessage(display, d, 25);
            else
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }            
        }

        task_heartbeat_grace(cycle_s * 1000 + 12000);
        vTaskDelay((cycle_s * 1000) / portTICK_PERIOD_MS);
    }
}
