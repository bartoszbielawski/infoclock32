// screen_wipe_task.cpp
// Provides wipe_on_release(), a pre-release hook for ResourceManager<LMDS>.
// Plays a random fancy transition effect between display hand-offs, but only
// once every `wipe_interval` minutes (config key, default 10).
// Set wipe_interval=0 to disable.

#include <Arduino.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>

static constexpr int kNumEffects = 13;

static void run_random_effect(LMDS& matrix) {
    int effect = (int)(esp_random() % kNumEffects);
    logPrintf("SCR", "wipe effect %d", effect);
    switch (effect) {
        case  0: wipeDisplayLeftToRight(matrix,  10); break;
        case  1: scrollOutDisplayRight(matrix,   10); break;
        case  2: scrollOutDisplayLeft(matrix,    10); break;
        case  3: wipeTopToBottom(matrix,         15); break;
        case  4: wipeCurtain(matrix,             15); break;
        case  5: wipeVenetianBlinds(matrix,      15); break;
        case  6: wipeStaticNoise(matrix,          3); break;
        case  7: scrollOutDisplayUp(matrix,      15); break;
        case  8: scrollOutDisplayDown(matrix,    15); break;
        case  9: wipeSpiralInward(matrix,         1); break;
        case 10: wipeDiagonal(matrix,             5); break;
        case 11: wipeSplitToCenter(matrix,        8); break;
        case 12: wipeColumnsFromCenter(matrix,    5); break;
    }
}

// Called by ResourceManager<LMDS>::release_access() before the resource is
// handed to the next task.  The caller still owns the display at this point.
void wipe_on_release(LMDS& matrix) {
    // A priority-lane request (clock, user push) is waiting: hand the display
    // over immediately — the wipe is decoration, the request is content.
    if (ResourceManager<LMDS>::getInstance().yieldRequested())
        return;

    // Throttle: the effect plays at most once per wipe_interval minutes
    // (config key, default 10, 0 disables entirely).
    static unsigned long lastWipeMs = 0;
    int intervalMin = max(0, DataStore::getInstance().get_value<int>("wipe_interval", 10));
    if (intervalMin == 0)
        return;
    unsigned long now = millis();
    if (lastWipeMs != 0 && now - lastWipeMs < (unsigned long)intervalMin * 60UL * 1000UL)
        return;
    lastWipeMs = now;

    run_random_effect(matrix);
    vTaskDelay(100/ portTICK_PERIOD_MS);
}
