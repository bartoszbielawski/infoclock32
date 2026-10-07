// Host checks for mqtt_discovery.hpp — the Home Assistant MQTT discovery
// config builder. Validates topic layout, the shared device/availability
// block, per-entity command/state topics and JSON string escaping.
#include <string>
#include <vector>

#include <mqtt_discovery.hpp>

using namespace ha_discovery;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) printf("ok   %s\n", (msg));                                   \
        else      { printf("FAIL %s\n", (msg)); failures++; }                   \
    } while (0)

static const DiscoveryMessage* find(const std::vector<DiscoveryMessage>& msgs,
                                    const std::string& topic)
{
    for (const auto& m : msgs)
        if (m.topic == topic) return &m;
    return nullptr;
}

int main()
{
    const std::string clientId = "infoclock32";
    DeviceMeta meta{"myclock", "AABBCCDDEEFF", "ESP32-C3", "v1.0"};
    auto msgs = build_discovery_messages(clientId, meta);

    CHECK(msgs.size() == 11, "11 discovery configs");

    // Common block on every entity: availability, device card, unique_id prefix.
    bool allHaveCommon = true;
    for (const auto& m : msgs)
    {
        if (m.topic.rfind("homeassistant/", 0) != 0) allHaveCommon = false;
        if (m.topic.find("/infoclock32_") == std::string::npos) allHaveCommon = false;
        if (m.topic.rfind("/config") != m.topic.size() - 7) allHaveCommon = false;
        if (m.payload.find("\"availability_topic\":\"infoclock32/availability\"") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"device\":{\"identifiers\":[\"AABBCCDDEEFF\"]") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"name\":\"myclock\"") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"model\":\"ESP32-C3\"") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"sw_version\":\"v1.0\"") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"unique_id\":\"infoclock32_") == std::string::npos) allHaveCommon = false;
        if (m.payload.find("\"manufacturer\":\"infoclock32\"") == std::string::npos) allHaveCommon = false;
    }
    CHECK(allHaveCommon, "every config has topic/device/availability/unique_id");

    // Availability is a dedicated topic — /status carries the JSON heartbeat,
    // which HA's exact-payload availability matcher would never recognize.
    CHECK(json_escape(clientId) == clientId, "simple clientId passes through unescaped");

    // Temperature: numeric template strips the °C suffix.
    const DiscoveryMessage* t = find(msgs, "homeassistant/sensor/infoclock32_temp_c/config");
    CHECK(t != nullptr, "temp_c sensor topic");
    if (t)
        CHECK(t->payload.find("\"value_template\":\"{{ value.split('°')[0] }}\"") != std::string::npos &&
              t->payload.find("\"state_topic\":\"infoclock32/publish/temp_c\"") != std::string::npos &&
              t->payload.find("\"device_class\":\"temperature\"") != std::string::npos,
              "temp_c strips unit, reads publish/temp_c");

    // Light: power for on/off, brightness 0-15 for the slider.
    const DiscoveryMessage* l = find(msgs, "homeassistant/light/infoclock32_display/config");
    CHECK(l != nullptr, "display light topic");
    if (l)
        CHECK(l->payload.find("\"state_topic\":\"infoclock32/publish/display_power\"") != std::string::npos &&
              l->payload.find("\"payload_on\":\"on\"") != std::string::npos &&
              l->payload.find("\"brightness_state_topic\":\"infoclock32/publish/display_brightness\"") != std::string::npos &&
              l->payload.find("\"brightness_scale\":15") != std::string::npos &&
              l->payload.find("\"command_topic\":\"infoclock32/power\"") != std::string::npos &&
              l->payload.find("\"brightness_command_topic\":\"infoclock32/brightness\"") != std::string::npos,
              "light maps power/brightness commands and states");

    // Command-only entities target the existing MQTT topics.
    const DiscoveryMessage* b = find(msgs, "homeassistant/button/infoclock32_reboot/config");
    CHECK(b != nullptr && b->payload.find("\"command_topic\":\"infoclock32/reboot\"") != std::string::npos,
          "reboot button targets /reboot");
    const DiscoveryMessage* x = find(msgs, "homeassistant/text/infoclock32_push/config");
    CHECK(x != nullptr && x->payload.find("\"command_topic\":\"infoclock32/push\"") != std::string::npos &&
          x->payload.find("\"max\":128") != std::string::npos,
          "push text targets /push, capped at 128");
    const DiscoveryMessage* s = find(msgs, "homeassistant/switch/infoclock32_power/config");
    CHECK(s != nullptr && s->payload.find("\"state_on\":\"on\"") != std::string::npos &&
          s->payload.find("\"state_topic\":\"infoclock32/publish/display_power\"") != std::string::npos,
          "power switch reads published state");

    // Uptime sensor is numeric, not the formatted "2d 4h…" string.
    CHECK(find(msgs, "homeassistant/sensor/infoclock32_uptime_s/config") != nullptr,
          "uptime_s sensor exists");
    CHECK(find(msgs, "homeassistant/sensor/infoclock32_weather_desc/config") != nullptr,
          "weather_desc sensor exists");

    // JSON escaping of quotes, backslashes and control chars.
    CHECK(json_escape("a\"b\\c\td") == "a\\\"b\\\\c\\u0009d", "json_escape quotes/backslash/tab");
    CHECK(json_escape("\x01") == "\\u0001", "json_escape control char");

    if (failures == 0) printf("all mqtt discovery checks passed\n");
    return failures == 0 ? 0 : 1;
}