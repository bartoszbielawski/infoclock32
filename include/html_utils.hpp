#pragma once

// HTML escaping for text that comes from outside the device (Wi-Fi SSIDs of
// neighbouring networks, ...). Pure C++, host-tested in
// tests/host/html_utils_check.cpp.

#include <string>

// Escapes text for HTML element content AND for a quoted attribute value
// (both ' and " are escaped). It is NOT safe for a JavaScript string: pass
// such values through a data-* attribute and read them via dataset instead.
inline std::string escapeHtml(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}
