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

// Read-only access to the in-memory log buffer (newest entry at back).
const std::deque<String> &getLogHistory();
