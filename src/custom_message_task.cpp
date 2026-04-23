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
        // Must end with "_text"
        const std::string suffix = "_text";
        if (key.size() < 8 + 1 + suffix.size()) continue;
        if (key.substr(key.size() - suffix.size()) != suffix) continue;

        // Extract slot name: between "message_" (8) and "_text" (5)
        std::string name = key.substr(8, key.size() - 13);
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
    registerTask("CustomMsg");
    auto& rmd = ResourceManager<LMDS>::getInstance();

    while (true)
    {
        auto messages = load_messages();
        time_t now    = time(nullptr);

        int cycle_s = DEFAULT_CYCLE_S;
        int v = DataStore::getInstance().get_value<int>("msg_interval", DEFAULT_CYCLE_S);
        v = max(10, min(3600, v));       

        for (const auto& m : messages)
        {
            // start is inclusive (>= midnight of start day)
            if (m.start >= 0 && now < m.start) continue;
            // end is inclusive — show through end of the end day
            if (m.end   >= 0 && now >= m.end + 86400) continue;

            std::string display = build_display(m);
            logPrintf("MSG", "%s", display.c_str());

            if (!rmd.make_access_request())
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            auto& matrix = rmd.getResourceRef();
            scrollMessage(display, matrix, 50);
            rmd.release_access();

            vTaskDelay(5000 / portTICK_PERIOD_MS);
        }

        vTaskDelay((cycle_s * 1000) / portTICK_PERIOD_MS);
    }
}
