#pragma once

#include <Arduino.h>
#include <deque>
#include <vector>

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
// NOT mutex-protected — safe only for single-core or when called from the same task as logPrintf.
const std::deque<LogEntry> &getLogHistory();

// Sequence number of the newest entry, or 0 if the buffer is empty. Mutex-protected.
uint32_t getLogSeq();

// Copies entries with seq > since into out (newest first) while holding the log mutex.
// Returns the current max seq. Use this for cross-task reads.
uint32_t copyLogEntriesSince(uint32_t since, std::vector<LogEntry>& out);
