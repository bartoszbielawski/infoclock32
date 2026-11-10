#pragma once

// Parsing of MQTT <client>/config payloads ("key=value"), pure and host-tested
// in tests/host/mqtt_config_check.cpp.
//
// The MQTT callback used to cut every payload at 128 bytes before parsing, so
// a longer /config value (a custom message text, say) was saved truncated
// without notice. /config now sees the full payload and refuses an over-long
// one instead.

#include <cstddef>
#include <string>
#include <config_utils.hpp>
#include <secret_keys.hpp>

constexpr size_t kMqttConfigMaxPayload = 512;

enum class MqttConfigResult { Ok, Disabled, TooLong, NoSeparator, InvalidKey, SecretKey };

inline const char* mqttConfigResultText(MqttConfigResult r)
{
    switch (r)
    {
        case MqttConfigResult::Ok:          return "ok";
        case MqttConfigResult::Disabled:    return "ignored (mqtt_config_writes=0)";
        case MqttConfigResult::TooLong:     return "refused: payload over 512 bytes";
        case MqttConfigResult::NoSeparator: return "refused: expected key=value";
        case MqttConfigResult::InvalidKey:  return "refused: invalid key";
        case MqttConfigResult::SecretKey:   return "refused: secret keys can't be set over MQTT";
    }
    return "?";
}

// `payload` need not be NUL-terminated. On Ok, key/value hold the parts.
inline MqttConfigResult parseMqttConfig(const char* payload, size_t len, bool writesEnabled,
                                        std::string& key, std::string& value)
{
    if (!writesEnabled) return MqttConfigResult::Disabled;
    if (len > kMqttConfigMaxPayload) return MqttConfigResult::TooLong;
    const std::string line(payload, len);
    const size_t sep = line.find('=');
    if (sep == std::string::npos || sep == 0) return MqttConfigResult::NoSeparator;
    key = line.substr(0, sep);
    value = line.substr(sep + 1);
    if (!isValidConfigKey(key)) return MqttConfigResult::InvalidKey;
    if (isSecretKey(key)) return MqttConfigResult::SecretKey;
    return MqttConfigResult::Ok;
}
