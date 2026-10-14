#pragma once

// Which config keys hold secrets. Used to keep their values out of logs
// (Serial, syslog, the in-memory log shown on /log) and to refuse remote
// writes over MQTT. Pure C++ so it is host-testable (tests/host/secret_keys_check.cpp).

#include <string>
#include <cctype>

// True for keys such as wifi_password, ota_password, web_password,
// mqtt_password, ow_api_key, novae_key, any *_secret / *_token.
inline bool isSecretKey(const std::string& key)
{
    std::string k;
    k.reserve(key.size());
    for (char c : key) k += (char)tolower((unsigned char)c);

    static const char* const contains[] = { "password", "passwd", "secret", "token", "api_key", "apikey", "appid" };
    for (const char* needle : contains)
        if (k.find(needle) != std::string::npos) return true;

    // *_key (and a bare "key")
    if (k == "key") return true;
    if (k.size() > 4 && k.compare(k.size() - 4, 4, "_key") == 0) return true;
    return false;
}

// Value as it may appear in a log line.
inline const char* loggableValue(const std::string& key, const std::string& value)
{
    return isSecretKey(key) ? (value.empty() ? "(empty)" : "***") : value.c_str();
}
