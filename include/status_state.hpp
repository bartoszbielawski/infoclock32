#pragma once
// Timed status ("ON AIR", "IN MEETING", ...): pure logic shared by the status
// task, the web/HTTP/MQTT triggers and the host tests. No Arduino/FreeRTOS here.
//
// A status replaces the normal rotation with fixed text for a fixed time.
// Durations are resolved to seconds once, at start, and then counted on the
// monotonic clock, so an SNTP step in the middle of a status cannot cut it
// short or stretch it. Wall time is needed only for "until end of the hour".

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

enum class StatusCue : uint8_t
{
    None,    // text only
    Border,  // steady bars at both ends of the matrix
    Blink,   // the same bars, blinking at 1 Hz
    Invert,  // whole frame inverted every second
};

constexpr size_t   kStatusMaxText     = 64;
constexpr uint32_t kStatusMinS        = 60;           // 1 minute
constexpr uint32_t kStatusMaxS        = 12 * 3600;    // 12 hours
constexpr int      kStatusMaxPresets  = 8;
constexpr uint32_t kStatusDefaultS    = 3600;
// "End of hour" with less than this left rolls over to the next hour: a
// 20-second status is never what someone tapping the button meant.
constexpr uint32_t kStatusEohMinS     = 60;

struct Status
{
    std::string text;
    StatusCue   cue        = StatusCue::None;
    uint32_t    durationS  = kStatusDefaultS;
    // Show the time for a few seconds every this many seconds; 0 = never.
    uint32_t    timeEveryS = 0;
};

struct StatusPreset
{
    int         slot = 0;      // N of the status_<N>_* keys (1-based)
    std::string text;
    StatusCue   cue = StatusCue::None;
    std::string duration = "60";   // minutes, or "eoh"
    int         timeEveryMin = 0;
};

enum class StatusError : uint8_t { Ok, EmptyText, BadDuration, NeedsClock, NoSuchPreset };

inline const char* status_error_text(StatusError e)
{
    switch (e)
    {
        case StatusError::Ok:          return "ok";
        case StatusError::EmptyText:   return "missing text";
        case StatusError::BadDuration: return "bad duration";
        case StatusError::NeedsClock:  return "end of hour needs a synced clock";
        case StatusError::NoSuchPreset: return "no such preset";
    }
    return "error";
}

inline StatusCue status_parse_cue(const std::string& s)
{
    if (s == "border") return StatusCue::Border;
    if (s == "blink")  return StatusCue::Blink;
    if (s == "invert") return StatusCue::Invert;
    return StatusCue::None;
}

inline const char* status_cue_name(StatusCue c)
{
    switch (c)
    {
        case StatusCue::Border: return "border";
        case StatusCue::Blink:  return "blink";
        case StatusCue::Invert: return "invert";
        case StatusCue::None:   break;
    }
    return "none";
}

// Trim, drop control characters, replace non-ASCII bytes (the matrix font is
// ASCII only) and cap the length.
inline std::string status_clean_text(const std::string& in)
{
    std::string out;
    for (char ch : in)
    {
        unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F) continue;
        out += (c > 0x7E) ? '?' : (char)c;
        if (out.size() >= kStatusMaxText) break;
    }
    size_t b = out.find_first_not_of(' ');
    if (b == std::string::npos) return "";
    size_t e = out.find_last_not_of(' ');
    return out.substr(b, e - b + 1);
}

// Seconds from `minute:second` of the current hour to the next full hour.
inline uint32_t status_seconds_to_hour_end(int minute, int second)
{
    int into = minute * 60 + second;
    if (into < 0) into = 0;
    if (into > 3599) into = 3599;
    return (uint32_t)(3600 - into);
}

// Resolve a duration spec to seconds. `spec` is a number of minutes
// ("1".."720") or "eoh" (until the end of the current hour). `clockValid`
// says whether minute/second come from a synced clock.
inline StatusError status_resolve_duration(const std::string& spec, bool clockValid,
                                           int minute, int second, uint32_t& outS)
{
    if (spec == "eoh" || spec == "hour")
    {
        if (!clockValid) return StatusError::NeedsClock;
        uint32_t s = status_seconds_to_hour_end(minute, second);
        if (s < kStatusEohMinS) s += 3600;
        outS = s;
        return StatusError::Ok;
    }
    if (spec.empty() || spec.size() > 4) return StatusError::BadDuration;
    for (char c : spec)
        if (c < '0' || c > '9') return StatusError::BadDuration;
    uint32_t s = (uint32_t)std::strtoul(spec.c_str(), nullptr, 10) * 60u;
    if (s < kStatusMinS || s > kStatusMaxS) return StatusError::BadDuration;
    outS = s;
    return StatusError::Ok;
}

inline StatusError status_make(const std::string& text, const std::string& cue,
                               const std::string& duration, int timeEveryMin,
                               bool clockValid, int minute, int second, Status& out)
{
    std::string clean = status_clean_text(text);
    if (clean.empty()) return StatusError::EmptyText;
    uint32_t secs = 0;
    StatusError e = status_resolve_duration(duration, clockValid, minute, second, secs);
    if (e != StatusError::Ok) return e;
    out.text       = clean;
    out.cue        = status_parse_cue(cue);
    out.durationS  = secs;
    out.timeEveryS = timeEveryMin > 0 ? (uint32_t)timeEveryMin * 60u : 0u;
    return StatusError::Ok;
}

// Whether a text `textW` pixels wide can stay static on a `matrixW` wide
// matrix. Cues that draw bars at both ends eat 2 px + 1 px gap per side.
inline bool status_fits(int textW, int matrixW, StatusCue cue)
{
    int margin = (cue == StatusCue::Border || cue == StatusCue::Blink) ? 6 : 0;
    return textW <= matrixW - margin;
}

// Built-in presets, used until the user defines status_1_text.
inline std::vector<StatusPreset> status_default_presets()
{
    std::vector<StatusPreset> p(3);
    for (int i = 0; i < 3; i++) p[i].slot = i + 1;
    p[0].text = "ON AIR";          p[0].cue = StatusCue::Blink;
    p[1].text = "IN MEETING";      p[1].cue = StatusCue::Border;
    p[2].text = "DO NOT DISTURB";  p[2].cue = StatusCue::Blink;
    return p;
}

// Read status_<N>_text/_cue/_minutes/_time for N = 1..kStatusMaxPresets.
// `get(key)` returns the config value or "" when unset. Slots with empty text
// are skipped; if no slot defines text the built-in presets are returned.
inline std::vector<StatusPreset> status_load_presets(
    const std::function<std::string(const std::string&)>& get)
{
    std::vector<StatusPreset> out;
    for (int n = 1; n <= kStatusMaxPresets; n++)
    {
        std::string pfx = "status_" + std::to_string(n) + "_";
        std::string text = status_clean_text(get(pfx + "text"));
        if (text.empty()) continue;
        StatusPreset p;
        p.slot = n;
        p.text = text;
        p.cue  = status_parse_cue(get(pfx + "cue"));
        std::string d = get(pfx + "minutes");
        if (!d.empty()) p.duration = d;
        std::string t = get(pfx + "time");
        if (!t.empty()) p.timeEveryMin = std::atoi(t.c_str());
        if (p.timeEveryMin < 0) p.timeEveryMin = 0;
        out.push_back(p);
    }
    if (out.empty()) return status_default_presets();
    return out;
}
