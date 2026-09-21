// Host-side regression tests for include/custom_message.hpp.
// Runs against the real header with stubbed stores — see run.sh.
#include <custom_message.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static time_t mk(int y, int mo, int d, int h, int mi = 0)
{
    struct tm t = {};
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = 0; t.tm_isdst = -1;
    return mktime(&t);
}

static CustomMessage slot(const char* text, time_t cd, time_t end = -1)
{
    CustomMessage m; m.text = text; m.start = -1; m.end = end; m.countdown = cd;
    return m;
}

int main()
{
    setenv("TZ", "Europe/Warsaw", 1);
    tzset();
    const time_t NOW = mk(2026, 9, 15, 14);

    // --- countdown_days: the day before the target must yield 1, not 0 ---
    CHECK(countdown_days(mk(2026,9,16,0), mk(2026,9,15,14))    == 1,  "day before 14:00 -> 1");
    CHECK(countdown_days(mk(2026,9,16,0), mk(2026,9,15,0,30))   == 1,  "day before 00:30 -> 1");
    CHECK(countdown_days(mk(2026,9,16,0), mk(2026,9,15,23,59))  == 1,  "day before 23:59 -> 1");
    CHECK(countdown_days(mk(2026,9,15,0), mk(2026,9,15,14))     == 0,  "target day -> 0");
    CHECK(countdown_days(mk(2026,9,15,0), mk(2026,9,16,0,30))   == -1, "day after -> -1");
    CHECK(countdown_days(mk(2026,9,15,0), mk(2026,9,18,14))     == -3, "3 days later -> -3");

    // --- DST safety (Europe/Warsaw: forward 2026-03-29, back 2026-10-25) ---
    CHECK(countdown_days(mk(2026,3,30,0), mk(2026,3,28,14))   == 2,   "23h-day span -> 2");
    CHECK(countdown_days(mk(2026,10,26,0), mk(2026,10,24,14)) == 2,   "25h-day span -> 2");
    CHECK(countdown_days(mk(2026,10,28,0), mk(2026,3,29,12))  == 213, "long span over 2 shifts -> 213");

    // --- day_end: start of next day, DST-aware ---
    CHECK(difftime(day_end(mk(2026,10,25,0)), mk(2026,10,25,0)) == 25 * 3600.0, "25h day end");
    CHECK(difftime(day_end(mk(2026,3,29,0)),  mk(2026,3,29,0))  == 23 * 3600.0, "23h day end");
    CHECK(difftime(day_end(mk(2026,9,15,0)),  mk(2026,9,15,0))  == 24 * 3600.0, "normal day end");
    // old code hid a slot ending on 2026-10-25 one hour early (end+86400):
    CHECK(mk(2026,10,25,23,30) < day_end(mk(2026,10,25,0)), "end of switch day still visible");

    // --- expiry: hidden from the day after the target unless end overrides ---
    const time_t tgt = mk(2026,9,15,0);
    CHECK(!is_expired(slot("X", tgt), mk(2026,9,15,23,59)),          "target day still visible");
    CHECK( is_expired(slot("X", tgt), mk(2026,9,16,0,30)),           "next day expired");
    CHECK(!is_expired(slot("X", tgt, mk(2026,9,20,0)), mk(2026,9,18,12)), "end overrides");
    CHECK(!is_expired(slot("plain", -1), NOW),                       "no countdown never expires");

    // --- placeholder expansion ---
    CHECK(expand_placeholders("Started {} days ago", -3, true) == "Started 3 days ago", "{} abs");
    CHECK(expand_placeholders("In {} days", 45, true)          == "In 45 days",        "{} positive");

    // --- build_display with an explicit clock ---
    CHECK(build_display(slot("Xmas", mk(2026,9,15,0)), NOW) == "Xmas: today!", "e2e target day");
    CHECK(build_display(slot("Xmas", mk(2026,9,17,0)), NOW) == "Xmas: 2d",     "e2e 2 days out");
    CHECK(build_display(slot("Xmas", mk(2026,9,16,0)), mk(2026,9,15,0,30)) == "Xmas: 1d", "e2e day before");
    CHECK(build_display(slot("Xmas", mk(2026,9,12,0), mk(2026,9,30,0)), NOW) == "Xmas: +3d", "e2e count-up");
    CHECK(build_display(slot("Party {}!", mk(2026,9,16,0)), NOW) == "Party 1!", "e2e placeholder");

    // --- parse_message_key ---
    std::string nm;
    CHECK( parse_message_key("message_hello_text", nm) && nm == "hello", "text key");
    CHECK( parse_message_key("message_x_text", nm)     && nm == "x",     "1-char name");
    CHECK(!parse_message_key("message_hello_start", nm), "non-text key");
    CHECK(!parse_message_key("message_text", nm),        "no name");
    CHECK(!parse_message_key("message__text", nm),       "empty name");
    CHECK(!parse_message_key("other_hello_text", nm),    "wrong prefix");

    printf(failures ? "%d test(s) FAILED\n" : "all tests passed\n", failures);
    return failures != 0;
}
