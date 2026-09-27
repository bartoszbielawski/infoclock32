#pragma once

void night_mode_task(void* parameter);

// True while the night-mode window is in effect (night_start..night_end).
bool night_mode_active();

// Show `level` (0-15) on the display and record it as the runtime value
// `display_brightness`. Does not touch the config.
void apply_display_brightness(int level);

// Level last requested via apply_display_brightness() (what the display is
// showing, night override included).
int current_display_brightness();

// Brightness change asked for by the user (MQTT /brightness, web /actions).
// By day it is shown and saved as the `brightness` config key. At night it is
// only shown, until the next day/night switch restores the configured level.
// Returns true when the level was saved to the config.
bool set_user_brightness(int level);
