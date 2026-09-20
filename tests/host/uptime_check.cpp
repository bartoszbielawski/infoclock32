// Host checks for uptime_utils.hpp — the shared uptime string used by the web
// UI, the MQTT heartbeat and DeviceStore.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <uptime_utils.hpp>

static int failures = 0;
#define CHECK(ms, expect)                                              \
    do {                                                               \
        char buf[32];                                                  \
        format_uptime(buf, sizeof(buf), (ms));                         \
        if (strcmp(buf, (expect)) == 0)                                \
            printf("ok   %-12lu -> \"%s\"\n", (unsigned long)(ms), buf); \
        else {                                                         \
            printf("FAIL %-12lu -> \"%s\" (expected \"%s\")\n",        \
                   (unsigned long)(ms), buf, (expect));                \
            failures++;                                                \
        }                                                              \
    } while (0)

int main()
{
    // Below a day the day field is omitted entirely.
    CHECK(0UL,          "0h 0m 0s");
    CHECK(43000UL,      "0h 0m 43s");
    CHECK(163000UL,     "0h 2m 43s");
    CHECK(3600000UL,    "1h 0m 0s");
    CHECK(14971000UL,   "4h 9m 31s");

    // Day boundary: hours must wrap at 24 rather than run unbounded.
    CHECK(86399000UL,   "23h 59m 59s");
    CHECK(86400000UL,   "1d 0h 0m 0s");
    CHECK(187771000UL,  "2d 4h 9m 31s");

    // millis() saturates at 2^32-1 (~49.7 days) before wrapping.
    CHECK(4294967295UL, "49d 17h 2m 47s");

    // Longest output must still fit the buf[32] callers pass.
    {
        char buf[32];
        format_uptime(buf, sizeof(buf), 4294967295UL);
        if (strlen(buf) < sizeof(buf)) printf("ok   longest output fits buf[32]\n");
        else { printf("FAIL longest output overflows buf[32]\n"); failures++; }
    }

    if (failures == 0) printf("all uptime checks passed\n");
    return failures == 0 ? 0 : 1;
}
