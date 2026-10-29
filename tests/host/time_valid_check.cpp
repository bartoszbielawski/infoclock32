// Host-side regression tests for include/time_valid.hpp and the date-dependence
// predicate in include/custom_message.hpp. Runs against the real headers — see run.sh.
#include <time_valid.hpp>
#include <custom_message.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    CHECK(!time_is_valid(0), "epoch 0 is not valid");
    CHECK(!time_is_valid(3600 * 1 + 16 * 60), "uptime-style 1970 value is not valid");
    CHECK(!time_is_valid(kMinValidTime - 1), "just before the threshold is invalid");
    CHECK(time_is_valid(kMinValidTime), "the threshold itself is valid");
    CHECK(time_is_valid(1793000000), "a 2026 timestamp is valid");

    CustomMessage plain;  plain.text = "hi";
    plain.start = plain.end = plain.countdown = -1;
    CHECK(!needs_wall_time(plain), "undated slot needs no clock");

    CustomMessage cd = plain;  cd.countdown = 1000;
    CustomMessage st = plain;  st.start = 1000;
    CustomMessage en = plain;  en.end = 1000;
    CHECK(needs_wall_time(cd), "countdown needs the clock");
    CHECK(needs_wall_time(st), "start date needs the clock");
    CHECK(needs_wall_time(en), "end date needs the clock");

    if (failures == 0) printf("time_valid_check: all passed\n");
    return failures ? 1 : 0;
}
