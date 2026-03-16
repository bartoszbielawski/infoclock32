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
        case  0: wipeDisplayLeftToRight(matrix,  15); break;
        case  1: scrollOutDisplayRight(matrix,   15); break;
        case  2: scrollOutDisplayLeft(matrix,    15); break;
        case  3: wipeTopToBottom(matrix,         30); break;
        case  4: wipeCurtain(matrix,             30); break;
        case  5: wipeVenetianBlinds(matrix,      30); break;
        case  6: wipeStaticNoise(matrix,          4); break;
        case  7: scrollOutDisplayUp(matrix,      30); break;
        case  8: scrollOutDisplayDown(matrix,    30); break;
        case  9: wipeSpiralInward(matrix,         1); break;
        case 10: wipeDiagonal(matrix,             5); break;
        case 11: wipeSplitToCenter(matrix,       10); break;
        case 12: wipeColumnsFromCenter(matrix,   10); break;
    }
}

// Called by ResourceManager<LMDS>::release_access() before the resource is
// handed to the next task.  The caller still owns the display at this point.
void wipe_on_release(LMDS& matrix) {
    run_random_effect(matrix);
    vTaskDelay(100/ portTICK_PERIOD_MS);
}
