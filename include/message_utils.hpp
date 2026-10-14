#pragma once

// Bounds for text that is rendered to the LED matrix (host-testable, see
// tests/host/message_utils_check.cpp).
//
// Every scroll/show helper builds a canvas that is 6 px wide per byte, and
// GFXcanvas width is int16_t: a long push message (HTTP /push, MQTT) would
// overflow it at ~5,400 characters and allocate tens of KB long before that.

#include <string>

inline constexpr size_t kMaxDisplayMessage = 256;   // bytes

// Truncate to at most `maxBytes`, never cutting a UTF-8 sequence in half.
inline std::string clampMessage(const std::string& s, size_t maxBytes = kMaxDisplayMessage)
{
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    // step back over continuation bytes (10xxxxxx) so the last char is whole
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        cut--;
    return s.substr(0, cut);
}
