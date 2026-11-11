// Host-side regression tests for include/status_state.hpp. See run.sh.
#include <status_state.hpp>
#include <cstdio>
#include <map>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static uint32_t dur(const char* spec, bool valid, int m, int s, StatusError* err = nullptr)
{
    uint32_t out = 0;
    StatusError e = status_resolve_duration(spec, valid, m, s, out);
    if (err) *err = e;
    return e == StatusError::Ok ? out : 0;
}

int main()
{
    // ── durations ────────────────────────────────────────────────────────────
    CHECK(dur("30", false, 0, 0) == 1800, "30 minutes");
    CHECK(dur("60", false, 0, 0) == 3600, "60 minutes");
    CHECK(dur("720", false, 0, 0) == 12 * 3600, "12 hours is the maximum");
    StatusError e;
    dur("721", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "above the maximum is rejected");
    dur("0", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "zero minutes is rejected");
    dur("", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "empty duration is rejected");
    dur("-5", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "negative is rejected");
    dur("1x", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "trailing junk is rejected");
    dur("99999", false, 0, 0, &e);
    CHECK(e == StatusError::BadDuration, "over-long number is rejected");

    // ── end of hour ──────────────────────────────────────────────────────────
    CHECK(dur("eoh", true, 10, 0) == 3000, "10:xx:00 -> 50 min");
    CHECK(dur("eoh", true, 59, 0) == 60, "59:00 -> exactly 1 min");
    CHECK(dur("eoh", true, 59, 30) == 3600 + 30, "59:30 rolls over to next hour end");
    CHECK(dur("eoh", true, 0, 0) == 3600, "top of the hour -> a full hour");
    CHECK(dur("hour", true, 30, 0) == 1800, "'hour' is an alias");
    dur("eoh", false, 10, 0, &e);
    CHECK(e == StatusError::NeedsClock, "eoh without a synced clock is refused");
    CHECK(status_seconds_to_hour_end(-3, -9) == 3600, "negative input is clamped");
    CHECK(status_seconds_to_hour_end(99, 99) == 1, "oversized input is clamped");

    // ── text ─────────────────────────────────────────────────────────────────
    CHECK(status_clean_text("  ON AIR  ") == "ON AIR", "trim");
    CHECK(status_clean_text("A\tB\nC") == "ABC", "control characters dropped");
    CHECK(status_clean_text("caf\xC3\xA9") == "caf??", "non-ASCII replaced");
    CHECK(status_clean_text("   ").empty(), "blank text becomes empty");
    CHECK(status_clean_text(std::string(200, 'x')).size() == kStatusMaxText, "length is capped");

    // ── cue ──────────────────────────────────────────────────────────────────
    CHECK(status_parse_cue("blink") == StatusCue::Blink, "blink");
    CHECK(status_parse_cue("border") == StatusCue::Border, "border");
    CHECK(status_parse_cue("invert") == StatusCue::Invert, "invert");
    CHECK(status_parse_cue("") == StatusCue::None, "empty cue");
    CHECK(status_parse_cue("sparkle") == StatusCue::None, "unknown cue falls back to none");
    CHECK(std::string(status_cue_name(StatusCue::Blink)) == "blink", "cue name round-trips");

    // ── status_make ──────────────────────────────────────────────────────────
    Status s;
    CHECK(status_make("ON AIR", "blink", "60", 0, true, 5, 0, s) == StatusError::Ok, "make ok");
    CHECK(s.text == "ON AIR" && s.cue == StatusCue::Blink && s.durationS == 3600 && s.timeEveryS == 0,
          "make fields");
    CHECK(status_make("x", "", "30", 5, true, 0, 0, s) == StatusError::Ok && s.timeEveryS == 300,
          "time-every is converted to seconds");
    CHECK(status_make("x", "", "30", -3, true, 0, 0, s) == StatusError::Ok && s.timeEveryS == 0,
          "negative time-every means never");
    CHECK(status_make("  ", "", "30", 0, true, 0, 0, s) == StatusError::EmptyText, "empty text refused");
    CHECK(status_make("x", "", "abc", 0, true, 0, 0, s) == StatusError::BadDuration, "bad duration refused");
    CHECK(status_make("x", "", "eoh", 0, false, 0, 0, s) == StatusError::NeedsClock, "eoh needs clock");

    // ── fit ──────────────────────────────────────────────────────────────────
    CHECK(status_fits(36, 64, StatusCue::None), "short text fits");
    CHECK(status_fits(64, 64, StatusCue::None), "exact width fits");
    CHECK(!status_fits(65, 64, StatusCue::None), "one pixel too wide scrolls");
    CHECK(status_fits(58, 64, StatusCue::Blink), "bars leave 58 px");
    CHECK(!status_fits(59, 64, StatusCue::Blink), "bars cost 6 px");
    CHECK(status_fits(64, 64, StatusCue::Invert), "invert needs no margin");

    // ── presets ──────────────────────────────────────────────────────────────
    std::map<std::string, std::string> cfg;
    auto get = [&cfg](const std::string& k) { auto it = cfg.find(k); return it == cfg.end() ? std::string() : it->second; };

    auto defs = status_load_presets(get);
    CHECK(defs.size() == 3 && defs[0].text == "ON AIR", "built-in presets when nothing configured");
    CHECK(defs[0].timeEveryMin == 0, "ON AIR does not show the time by default");
    CHECK(defs[0].slot == 1 && defs[2].slot == 3, "built-in presets are slots 1..3");

    cfg["status_2_text"] = "LUNCH";
    cfg["status_2_cue"] = "border";
    cfg["status_2_minutes"] = "eoh";
    cfg["status_2_time"] = "10";
    cfg["status_5_text"] = "   ";
    cfg["status_6_text"] = "FOCUS";
    cfg["status_6_time"] = "-4";
    auto ps = status_load_presets(get);
    CHECK(ps.size() == 2, "configured slots replace the built-ins (blank slot skipped)");
    CHECK(ps[0].text == "LUNCH" && ps[0].cue == StatusCue::Border && ps[0].duration == "eoh" &&
          ps[0].timeEveryMin == 10, "preset fields");
    CHECK(ps[1].text == "FOCUS" && ps[1].duration == "60" && ps[1].timeEveryMin == 0,
          "defaults and negative time clamped");
    CHECK(ps[0].slot == 2 && ps[1].slot == 6, "slot numbers are kept when others are blank");

    if (failures == 0) printf("status_state_check: all passed\n");
    return failures ? 1 : 0;
}
