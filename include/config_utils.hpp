#pragma once

// Pure helpers for config handling and log hygiene (host-testable, see
// tests/host/config_utils_check.cpp).

#include <string>
#include <cctype>
#include <secret_keys.hpp>

// config.txt is line based ("key=value\n"). A value containing CR/LF would
// split into a second line on the next save and be parsed as another key
// (e.g. an MQTT /config payload smuggling "web_password=..."), so line breaks
// are folded to spaces before a value is stored.
inline std::string sanitizeConfigValue(const std::string& value)
{
    std::string out = value;
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}

// A key must be non-empty, free of '=' and control characters/whitespace
// padding, and must not start with '#' (it would read back as a comment).
inline bool isValidConfigKey(const std::string& key)
{
    if (key.empty() || key[0] == '#') return false;
    for (char c : key)
    {
        unsigned char u = (unsigned char)c;
        if (c == '=' || u < 0x20 || u == 0x7f) return false;
    }
    return true;
}

// Mask the value of secret query parameters (appid=, api_key=, token=, ...)
// so request URLs can be logged without leaking credentials.
inline std::string redactUrl(const std::string& url)
{
    size_t q = url.find('?');
    if (q == std::string::npos) return url;

    std::string out = url.substr(0, q + 1);
    size_t pos = q + 1;
    while (pos <= url.size())
    {
        size_t amp = url.find('&', pos);
        if (amp == std::string::npos) amp = url.size();
        std::string param = url.substr(pos, amp - pos);
        size_t eq = param.find('=');
        if (eq != std::string::npos && isSecretKey(param.substr(0, eq)))
            param = param.substr(0, eq + 1) + "***";
        out += param;
        if (amp < url.size()) out += '&';
        pos = amp + 1;
    }
    return out;
}
