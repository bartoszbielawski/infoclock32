#pragma once

// FreeRTOS task that displays custom messages from config.txt.
//
// Config keys (all optional except message_<name>_text):
//   message_<name>_text      = text to scroll (required — activates the slot)
//   message_<name>_start     = YYYY-MM-DD — don't show before this date (inclusive)
//   message_<name>_end       = YYYY-MM-DD — don't show after this date (inclusive)
//   message_<name>_countdown = YYYY-MM-DD — target date; adds day counting
//
// Display format — two modes depending on whether text contains "{}":
//
//   Placeholder mode (text has "{}"):
//     {} is replaced with the absolute day count; you write the unit.
//     "LS3 will start in {} days!"  →  "LS3 will start in 45 days!"
//     "LS3 started {} days ago!"    →  "LS3 started 3 days ago!"
//     Tip: use message_<name>_end / _start to show only the right variant.
//
//   Append mode (no "{}"):
//     Suffix is appended automatically.
//     Counting down:   "Christmas: 45d"
//     Day of event:    "Christmas: today!"
//     Counting up:     "Christmas: +5d"
//
// Slots are discovered by scanning message_*_text keys. A slot is shown when
// its text is non-empty and the current date falls within [start, end]
// (both bounds inclusive). Day counts are calendar days (local midnight to
// local midnight) and all day arithmetic is DST-safe, so "today!" appears
// only on the target day itself.
//
// A countdown slot with no explicit end auto-hides from the day after its
// target; set end to a later date to keep counting up ("+Nd" / "{} days ago").
//
// Cycle interval is configurable via msg_interval (seconds, default 60).
void custom_message_task(void* parameter);
