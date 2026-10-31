#pragma once

// Parser for /config.txt (key=value lines). Pure C++, host-tested in
// tests/host/config_parse_check.cpp. DataStore::load_from_file() parses into a
// temporary map with this and swaps it in only once the whole file was read,
// so deleted keys really disappear and a failed read keeps the old config.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// A line longer than this is ignored whole: its tail must not be taken for a
// fresh key=value line.
constexpr size_t CONFIG_MAX_LINE = 127;

inline std::map<std::string, std::string>
parseConfigText(const std::string& text,
                std::vector<std::string>* tooLong = nullptr)
{
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;

        if (line.empty() || line[0] == '#') continue;
        if (line.size() > CONFIG_MAX_LINE) {
            if (tooLong) tooLong->push_back(line.substr(0, 24));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        // CRLF files: the '\n' is the separator, the '\r' must not leak in.
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
        while (!key.empty()   && (key.back()   == '\r' || key.back()   == '\n')) key.pop_back();
        out[key] = value;
    }
    return out;
}
