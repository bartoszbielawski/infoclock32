#pragma once

// Text helpers for the LHC status task (pure, host-tested in
// tests/host/lhc_text_check.cpp).
//
// Data source: CERN's public LHC Page 1 (webfd.app.cern.ch/lhc-vistar-page1),
// /history?channels=status,comments. The operator comment there is HTML,
// e.g. "<center><br>So long LHC!<br>Let&#8217;s follow the road</center>".

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <display_strings.hpp>

// Shown between the lines of a multi-line comment (where the HTML had <br>,
// <p>, ...). It is the separator glyph the old RSS parser used.
static constexpr char kLhcLineBreak = '\x07';

namespace lhc_detail
{
inline void appendUtf8(std::string& out, unsigned long cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
    // beyond the BMP (emoji, ...): dropped, the matrix font cannot show it
}

// Code point → text: typographic punctuation becomes ASCII here, Latin
// letters go out as UTF-8 for ascii_fold() to transliterate.
inline void appendCodePoint(std::string& out, unsigned long cp)
{
    switch (cp) {
        case 0xA0:   out += ' ';   return;  // no-break space
        case 0x2018: case 0x2019: case 0x201A: case 0x2032:
                     out += '\'';  return;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033:
                     out += '"';   return;
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212:
                     out += '-';   return;
        case 0x2022: case 0xB7: out += '*'; return;  // bullet, middle dot
        case 0x2026: out += "..."; return;
        case 0xB0:   out += "deg"; return;
    }
    if (cp == 0 || (cp < 0x20 && cp != '\t' && cp != '\n')) return;  // controls
    appendUtf8(out, cp);
}

inline bool isBreakTag(const std::string& name)
{
    // name is upper-case and without the leading '/'
    return name == "BR" || name == "P" || name == "DIV" || name == "LI" ||
           name == "TR" || name == "CENTER" || name == "H1" || name == "H2" ||
           name == "H3" || name == "H4";
}

// Decodes the entity starting at s[i] == '&'. On success appends the text,
// moves i past the ';' and returns true.
inline bool decodeEntity(const std::string& s, size_t& i, std::string& out)
{
    size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) return false;
    std::string name = s.substr(i + 1, semi - i - 1);
    if (name.empty()) return false;

    if (name[0] == '#') {
        bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const char* digits = name.c_str() + (hex ? 2 : 1);
        if (!*digits) return false;
        char* end = nullptr;
        unsigned long cp = strtoul(digits, &end, hex ? 16 : 10);
        if (*end) return false;
        appendCodePoint(out, cp);
        i = semi + 1;
        return true;
    }

    static const struct { const char* name; unsigned long cp; } kNamed[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", 0xA0}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026},
        {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"bull", 0x2022}, {"middot", 0xB7}, {"deg", 0xB0}, {"minus", 0x2212},
        {"eacute", 0xE9}, {"egrave", 0xE8}, {"agrave", 0xE0}, {"ccedil", 0xE7},
        {"ouml", 0xF6}, {"uuml", 0xFC}, {"auml", 0xE4}, {"szlig", 0xDF},
    };
    for (const auto& e : kNamed) {
        if (name == e.name) {
            appendCodePoint(out, e.cp);
            i = semi + 1;
            return true;
        }
    }
    return false;
}
}  // namespace lhc_detail

// HTML fragment → one line for the matrix: tags removed, line-breaking tags
// turned into a single kLhcLineBreak (spaced, never leading or trailing),
// entities decoded, whitespace collapsed, non-ASCII folded via ascii_fold().
inline std::string lhcHtmlToText(const std::string& html)
{
    using namespace lhc_detail;
    std::string text;   // decoded, kLhcLineBreak where lines break
    text.reserve(html.size());
    for (size_t i = 0; i < html.size();) {
        char c = html[i];
        if (c == '<') {
            size_t close = html.find('>', i);
            if (close == std::string::npos) break;  // unterminated tag: drop the rest
            size_t b = i + 1;
            if (b < close && html[b] == '/') b++;
            size_t e = b;
            while (e < close && (isalnum((unsigned char)html[e]))) e++;
            std::string name = html.substr(b, e - b);
            for (char& ch : name) ch = (char)toupper((unsigned char)ch);
            if (isBreakTag(name)) text += kLhcLineBreak;
            i = close + 1;
        } else if (c == '&' && decodeEntity(html, i, text)) {
            // decoded
        } else {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            if (c != kLhcLineBreak) text += c;
            i++;
        }
    }

    const std::string folded = ascii_fold(text.c_str());

    // Collapse: runs of spaces → one space; runs of breaks (with any spaces
    // between them) → " \x07 "; nothing at either end.
    std::string out;
    bool pendingSpace = false, pendingBreak = false;
    for (char c : folded) {
        if (c == ' ') { pendingSpace = true; continue; }
        if (c == kLhcLineBreak) { pendingBreak = true; continue; }
        if (!out.empty()) {
            if (pendingBreak) { out += ' '; out += kLhcLineBreak; out += ' '; }
            else if (pendingSpace) out += ' ';
        }
        pendingSpace = pendingBreak = false;
        out += c;
    }
    return out;
}

// "SHUTDOWN: NO BEAM", or "PROTON PHYSICS: STABLE BEAMS @ 6800 GeV" once
// there is beam energy. Empty when the machine mode is missing.
inline std::string lhcStatusLine(const std::string& mode, const std::string& state,
                                 double energyGev)
{
    if (mode.empty()) return {};
    std::string line = mode;
    if (!state.empty()) line += ": " + state;
    if (energyGev >= 1.0) {
        char buf[24];
        snprintf(buf, sizeof(buf), " @ %.0f GeV", energyGev);
        line += buf;
    }
    return line;
}
