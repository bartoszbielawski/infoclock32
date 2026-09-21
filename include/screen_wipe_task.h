#pragma once
#include <LMDS.hpp>

// Pre-release hook for ResourceManager<LMDS>.
// Register with: ResourceManager<LMDS>::getInstance().setPreReleaseHook(wipe_on_release);
void wipe_on_release(LMDS& matrix);
