#pragma once

#include <Arduino.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <display_strings.hpp>

inline void reboot_with_message()
{
    if (auto display = ResourceManager<LMDS>::getInstance().acquire(pdMS_TO_TICKS(2000)))
        scrollMessage(display_strings(parse_lang(DataStore::getInstance().get_value("language", "en"))).rebooting,
                      display, 30);
    ESP.restart();
}
