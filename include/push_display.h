#pragma once

#include <string>

// One-time messages (web /push, /actions, MQTT /push): queued here and
// scrolled once by a short-lived "Push" task on the display's priority lane.
// The task exists only while messages are waiting, so callers never block on
// the display and an idle clock pays no stack for it.
//
// While a timed status is active the task exits and messages stay queued;
// the status task calls push_display_kick() when it ends.

// Create the queue; call once from setup() before any task can push.
void push_display_init();

// Queue `text` to scroll once at `speedMs` per pixel step. Non-blocking.
// Returns false (message dropped) when the queue is full or out of memory.
bool show_once(const std::string& text, int speedMs);

// Start the task if messages are waiting (no-op otherwise).
void push_display_kick();
