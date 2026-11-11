#pragma once

// Timed status display ("ON AIR", "IN MEETING", ...).
//
// While a status is active, StatusTask holds the display exclusively: the
// normal rotation (clock, weather, menus, sensors, messages) is parked, then
// resumes with fresh data once the status expires or is cancelled. Nothing
// is persisted: a reboot or WiFi loss returns to the normal rotation.
//
// All entry points are non-blocking and safe from any task (web, MQTT).
//
// Config keys (N = 1..8, all optional; built-in presets until status_1_text
// or any other slot defines text):
//   status_<N>_text    = text shown (ASCII, max 64 chars)
//   status_<N>_cue     = none | border | blink | invert
//   status_<N>_minutes = duration in minutes (1..720) or "eoh" (end of hour)
//   status_<N>_time    = show the time for 3 s every N minutes (0 = never)

#include <string>
#include <vector>

#include <status_state.hpp>

struct StatusInfo
{
    bool        active = false;     // requested or showing
    std::string text;
    StatusCue   cue = StatusCue::None;
    uint32_t    totalS = 0;
    uint32_t    remainingS = 0;
};

void status_task(void* parameter);

// Start a status, or replace the running one (the timer restarts).
void status_start(const Status& s);
void status_cancel();
StatusInfo status_info();

// Presets from config (or the built-ins).
std::vector<StatusPreset> status_presets();

// Start the preset in `slot`. `duration` overrides its default when non-empty
// (minutes or "eoh").
StatusError status_request_preset(int slot, const std::string& duration);

// Start a free-text status; `cue` and `duration` may be empty (none, 60 min).
StatusError status_request_custom(const std::string& text, const std::string& cue,
                                  const std::string& duration);
