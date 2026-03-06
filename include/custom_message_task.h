#pragma once

// FreeRTOS task that displays custom messages from config.txt.
//
// Config keys (all optional except msg<N>_text):
//   msg<N>_text      = text to scroll (required — activates slot N)
//   msg<N>_start     = YYYY-MM-DD — don't show before this date
//   msg<N>_end       = YYYY-MM-DD — don't show after this date (inclusive)
//   msg<N>_countdown = YYYY-MM-DD — target date; appends countdown/countup suffix
//
// Display format — two modes depending on whether text contains "{}":
//
//   Placeholder mode (text has "{}"):
//     {} is replaced with the absolute day count; you write the unit.
//     "LS3 will start in {} days!"  →  "LS3 will start in 45 days!"
//     "LS3 started {} days ago!"   →  "LS3 started 3 days ago!"
//     Tip: use msg<N>_end / msg<N>_start to show only the right variant.
//
//   Append mode (no "{}"):
//     Suffix is appended automatically.
//     Counting down:   "Christmas: 45d"
//     Day of event:    "Christmas: today!"
//     Counting up:     "Christmas: +5d"
//
// Slots msg1..msg8 are scanned. A slot is active when msg<N>_text exists
// and the current date falls within [start, end] (both bounds inclusive).
//
// Cycle interval is configurable via msg_interval (seconds, default 60).
void custom_message_task(void* parameter);
