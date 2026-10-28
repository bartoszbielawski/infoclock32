#pragma once
// Pure parser for the serial console (src/serial_console_task.cpp).
// Lines look like "set key=value", "get key", "reboot", "wifi_forget", "help".
#include <string>

enum class SerialCmd { None, Set, Get, Reboot, WifiForget, WifiScan, WifiCache, Help, Unknown };

struct SerialCommand {
    SerialCmd   cmd = SerialCmd::None;
    std::string key;
    std::string value;
};

inline std::string serialTrim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline SerialCommand parseSerialCommand(const std::string& rawLine)
{
    SerialCommand c;
    std::string line = serialTrim(rawLine);
    if (line.empty()) return c;

    size_t sp = line.find_first_of(" \t");
    std::string verb = line.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : serialTrim(line.substr(sp + 1));

    if (verb == "set") {
        size_t eq = rest.find('=');
        if (eq == std::string::npos || eq == 0) { c.cmd = SerialCmd::Unknown; return c; }
        c.cmd   = SerialCmd::Set;
        c.key   = serialTrim(rest.substr(0, eq));
        c.value = rest.substr(eq + 1);   // value kept verbatim (may contain spaces)
        if (c.key.empty()) c.cmd = SerialCmd::Unknown;
    } else if (verb == "wifi_cache") {      // wifi_cache ssid=password (test hook)
        size_t eq = rest.find('=');
        if (eq == std::string::npos || eq == 0) { c.cmd = SerialCmd::Unknown; return c; }
        c.cmd   = SerialCmd::WifiCache;
        c.key   = rest.substr(0, eq);
        c.value = rest.substr(eq + 1);
    } else if (verb == "get") {
        if (rest.empty()) { c.cmd = SerialCmd::Unknown; return c; }
        c.cmd = SerialCmd::Get;
        c.key = rest;
    } else if (verb == "reboot")           c.cmd = SerialCmd::Reboot;
    else if (verb == "wifi_forget")        c.cmd = SerialCmd::WifiForget;
    else if (verb == "wifi_scan")          c.cmd = SerialCmd::WifiScan;
    else if (verb == "help")               c.cmd = SerialCmd::Help;
    else                                   c.cmd = SerialCmd::Unknown;
    return c;
}
