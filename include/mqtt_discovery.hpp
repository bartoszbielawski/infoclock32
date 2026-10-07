#pragma once
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// Pure helpers for Home Assistant MQTT discovery — no hardware or framework
// dependencies, so the whole header compiles and runs in the host tests.
//
// On every MQTT (re)connect the device publishes retained discovery configs
// under the standard `homeassistant/` prefix; entity states go to the same
// `<clientId>/publish/<key>` topics the `…/request` path answers on, refreshed
// by the 60 s heartbeat. Availability rides the `/status` heartbeat, with a
// retained last-will "offline" so HA marks the device unavailable on death.

namespace ha_discovery {

// Device block shared by all entities — groups them under one card in HA.
struct DeviceMeta
{
    std::string name;        // shown in HA (hostname)
    std::string identifiers; // unique per device (MAC)
    std::string model;       // chip model
    std::string sw_version;  // APP_VERSION
};

// Escape a string for inclusion inside a JSON string literal.
inline std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        }
        else out += c;
    }
    return out;
}

// Availability topic — dedicated, because the /status heartbeat publishes
// JSON that HA's exact-payload availability matcher would never recognize.
inline std::string availability_topic(const std::string& clientId)
{
    return clientId + "/availability";
}

// State topic — the same /publish/<key> topics `…/request` answers on.
inline std::string state_topic(const std::string& clientId, const char* key)
{
    return clientId + "/publish/" + key;
}

// Discovery topic: homeassistant/<component>/<clientId>_<objectId>/config
inline std::string config_topic(const std::string& clientId, const char* component,
                                const char* objectId)
{
    return "homeassistant/" + std::string(component) + "/" + clientId + "_" +
           objectId + "/config";
}

struct DiscoveryMessage
{
    std::string topic;
    std::string payload;
};

// Full discovery JSON for one entity.
inline std::string config_payload(const std::string& clientId, const char* objectId,
                                  const char* name, const DeviceMeta& device,
                                  const std::string& fields)
{
    std::string out;
    out.reserve(224 + fields.size());
    out += "{\"name\":\"" + json_escape(name) + "\"";
    out += ",\"unique_id\":\"" + json_escape(clientId) + "_" + objectId + "\"";
    out += ",\"availability_topic\":\"" + availability_topic(clientId) + "\"";
    out += ",\"device\":{\"identifiers\":[\"" + json_escape(device.identifiers) + "\"]";
    out += ",\"name\":\"" + json_escape(device.name) + "\"";
    out += ",\"manufacturer\":\"infoclock32\"";
    out += ",\"model\":\"" + json_escape(device.model) + "\"";
    out += ",\"sw_version\":\"" + json_escape(device.sw_version) + "\"}";
    if (!fields.empty()) out += "," + fields;
    out += "}";
    return out;
}

// All discovery configs, published retained on every (re)connect.
inline std::vector<DiscoveryMessage> build_discovery_messages(const std::string& clientId,
                                                              const DeviceMeta& device)
{
    std::vector<DiscoveryMessage> msgs;
    auto pub = [&](const char* key) { return state_topic(clientId, key); };
    auto add = [&](const char* component, const char* objectId, const char* name,
                   const std::string& fields)
    {
        DiscoveryMessage m;
        m.topic   = config_topic(clientId, component, objectId);
        m.payload = config_payload(clientId, objectId, name, device, fields);
        msgs.push_back(std::move(m));
    };

    add("sensor", "temp_c", "Temperature",
        "\"state_topic\":\"" + pub("temp_c") + "\""
        ",\"device_class\":\"temperature\""
        ",\"unit_of_measurement\":\"°C\""
        ",\"value_template\":\"{{ value.split('°')[0] }}\""
        ",\"state_class\":\"measurement\"");
    add("sensor", "temp_rh", "Humidity",
        "\"state_topic\":\"" + pub("temp_rh") + "\""
        ",\"device_class\":\"humidity\""
        ",\"unit_of_measurement\":\"%\""
        ",\"state_class\":\"measurement\"");
    add("sensor", "temp_hpa", "Pressure",
        "\"state_topic\":\"" + pub("temp_hpa") + "\""
        ",\"device_class\":\"pressure\""
        ",\"unit_of_measurement\":\"hPa\""
        ",\"state_class\":\"measurement\"");
    add("sensor", "weather_desc", "Weather",
        "\"state_topic\":\"" + pub("weather_desc") + "\"");
    add("sensor", "heap", "Free heap",
        "\"state_topic\":\"" + pub("heap") + "\""
        ",\"unit_of_measurement\":\"B\""
        ",\"state_class\":\"measurement\""
        ",\"icon\":\"mdi:memory\"");
    add("sensor", "rssi", "WiFi signal",
        "\"state_topic\":\"" + pub("rssi") + "\""
        ",\"device_class\":\"signal_strength\""
        ",\"unit_of_measurement\":\"dBm\""
        ",\"state_class\":\"measurement\"");
    add("sensor", "uptime_s", "Uptime",
        "\"state_topic\":\"" + pub("uptime_s") + "\""
        ",\"unit_of_measurement\":\"s\""
        ",\"state_class\":\"total_increasing\""
        ",\"icon\":\"mdi:timer-outline\"");
    add("light", "display", "Display",
        "\"state_topic\":\"" + pub("display_power") + "\""
        ",\"payload_on\":\"on\""
        ",\"payload_off\":\"off\""
        ",\"brightness_state_topic\":\"" + pub("display_brightness") + "\""
        ",\"brightness_value_template\":\"{{ value|int }}\""
        ",\"brightness_scale\":15"
        ",\"command_topic\":\"" + clientId + "/power\""
        ",\"brightness_command_topic\":\"" + clientId + "/brightness\"");
    add("switch", "power", "Display power",
        "\"command_topic\":\"" + clientId + "/power\""
        ",\"state_topic\":\"" + pub("display_power") + "\""
        ",\"state_on\":\"on\""
        ",\"state_off\":\"off\""
        ",\"icon\":\"mdi:monitor\"");
    add("text", "push", "Scroll message",
        "\"command_topic\":\"" + clientId + "/push\""
        ",\"mode\":\"text\""
        ",\"min\":0"
        ",\"max\":128");
    add("button", "reboot", "Reboot",
        "\"command_topic\":\"" + clientId + "/reboot\""
        ",\"device_class\":\"restart\"");

    return msgs;
}

} // namespace ha_discovery