#pragma once

#include <Arduino.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>

inline void reboot_with_message()
{
    if (auto display = ResourceManager<LMDS>::getInstance().acquire(pdMS_TO_TICKS(2000)))
        scrollMessage("Rebooting", display, 30);
    ESP.restart();
}
