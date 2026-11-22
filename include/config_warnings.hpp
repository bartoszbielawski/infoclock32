#pragma once

#include <vector>

#include <deprecated_keys.hpp>

// Deprecated config keys in use right now, computed from the live config
// (shown as a banner on the web pages, see web_ui).
std::vector<DeprecatedUse> config_warnings();

// One log line per warning; call once at boot after the config is loaded.
void config_warnings_log();

// Housekeeping tick (cheap, call every second): every
// `config_warn_interval_min` minutes (default 60, 0 = off), scroll a
// "Check your config" reminder while any warning exists.
void config_warning_tick();
