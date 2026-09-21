// Host checks for watchdog_utils.hpp — wrap-safe staleness math.
#include <cstdint>
#include <cstdio>

#include <watchdog_utils.hpp>

static int failures = 0;
#define CHECK(cond, msg)                               \
    do {                                               \
        if (cond) printf("ok   %s\n", msg);            \
        else { printf("FAIL %s\n", msg); failures++; } \
    } while (0)

int main()
{
    // Fresh task: well inside the timeout.
    CHECK(!wdt_is_stale(10000, 9000, 0, 60000), "recent beat is not stale");

    // Exactly on the limit: strict >, so still healthy.
    CHECK(!wdt_is_stale(70000, 10000, 0, 60000), "exact timeout is not stale");
    CHECK(wdt_is_stale(70001, 10000, 0, 60000), "past timeout is stale");

    // Grace extends the window.
    CHECK(!wdt_is_stale(70001, 10000, 60000, 60000), "grace extends the window");
    CHECK(wdt_is_stale(130001, 10000, 60000, 60000), "grace + timeout expiry is stale");

    // millis() wrap (~49.7 days): lastSeen just before the wrap.
    CHECK(!wdt_is_stale(0x00000040u, 0xFFFFFF80u, 0, 300), "recent beat across wrap");
    CHECK(wdt_is_stale(0x00000130u, 0xFFFFFF80u, 0, 300), "stale across wrap");
    CHECK(!wdt_is_stale(0x00000130u, 0xFFFFFF80u, 300, 300), "grace across wrap");

    if (failures == 0) printf("all watchdog checks passed\n");
    return failures == 0 ? 0 : 1;
}
