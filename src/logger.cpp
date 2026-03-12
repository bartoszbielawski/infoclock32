#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <data_store.hpp>
#include <logger.hpp>

#include <deque>
#include <cstdarg>

static constexpr size_t MAX_HISTORY   = 40;
static constexpr uint16_t SYSLOG_PORT = 514;

static std::deque<LogEntry> logHistory;
static uint32_t             logNextSeq = 1; // seq 0 is reserved as "nothing seen yet"
static SemaphoreHandle_t   logMutex   = nullptr;
static WiFiUDP             udp;
static String              syslogServer;

// ── helpers ──────────────────────────────────────────────────────────────────

static void getTimestamp(char *buf, size_t len)
{
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

static void sendSyslog(const char *tag, const char *msg)
{
    if (WiFi.status() != WL_CONNECTED) return;
    if (syslogServer.isEmpty()) return;
    if (!udp.beginPacket(syslogServer.c_str(), SYSLOG_PORT)) return;

    char ts[24];
    getTimestamp(ts, sizeof(ts));

    // RFC 3164 — facility 1 (user), severity 6 (info) → priority 14
    udp.printf("<14>1 %s %s %s - - - %s", ts, WiFi.getHostname(), tag, msg);
    udp.endPacket();
}

// ── public API ────────────────────────────────────────────────────────────────

void logger_init()
{
    logMutex = xSemaphoreCreateMutex();
    syslogServer = DataStore::getInstance().get_value("syslog_server", "").c_str();
    if (!syslogServer.isEmpty())
        Serial.printf("Logger: syslog → %s:%d\n", syslogServer.c_str(), SYSLOG_PORT);
}

void logPrintf(const char *tag, const char *format, ...)
{
    char msgBuf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(msgBuf, sizeof(msgBuf), format, args);
    va_end(args);

    char ts[24];
    getTimestamp(ts, sizeof(ts));

    // Full line: "YYYY-MM-DDTHH:MM:SS [TAG] message"
    char line[256];
    snprintf(line, sizeof(line), "%s [%s] %s", ts, tag, msgBuf);

    Serial.println(line);
    sendSyslog(tag, msgBuf);

    if (logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        logHistory.push_back({logNextSeq++, String(line)});
        while (logHistory.size() > MAX_HISTORY)
            logHistory.pop_front();
        xSemaphoreGive(logMutex);
    }
}

const std::deque<LogEntry> &getLogHistory()
{
    return logHistory;
}

uint32_t getLogSeq()
{
    return logHistory.empty() ? 0 : logHistory.back().seq;
}
