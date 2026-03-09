#pragma once

#include <Arduino.h>
#include <deque>

// Initialise the logger. Call once from setup() after dataStore.load_from_file().
// Reads "syslog_server" from DataStore and creates the internal mutex.
void logger_init();

// Printf-style log. Writes to Serial, syslog UDP, and in-memory FIFO.
// tag  — short component name shown in every line, e.g. "MQT", "LHC"
// format — printf format string (stored in flash is fine)
void logPrintf(const char *tag, const char *format, ...);

// One entry in the in-memory log buffer.
struct LogEntry {
    uint32_t seq;   // monotonically increasing, starts at 1
    String   line;  // "YYYY-MM-DDTHH:MM:SS [TAG] message"
};

// Read-only access to the in-memory log buffer (oldest entry at front, newest at back).
const std::deque<LogEntry> &getLogHistory();

// Sequence number of the newest entry, or 0 if the buffer is empty.
uint32_t getLogSeq();
