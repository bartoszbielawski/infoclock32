// Host-side tests for include/log_policy.hpp.
#include <log_policy.hpp>
#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    CHECK(!logLineKept("DISP", false), "display lines dropped from history by default");
    CHECK(logLineKept("DISP", true), "log_display=1 keeps them");
    for (const char* tag : {"SYS", "WIFI", "WEB", "TMP", "MQT", "SEC", "WDT", "DISPX", "DIS"})
        CHECK(logLineKept(tag, false), "'%s' always kept", tag);
    CHECK(logLineKept(nullptr, false), "no tag kept");

    if (failures) { printf("%d log policy check(s) FAILED\n", failures); return 1; }
    printf("all log policy checks passed\n");
    return 0;
}
