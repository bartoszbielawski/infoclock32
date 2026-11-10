// Host-side tests for include/mqtt_config.hpp.
#include <mqtt_config.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static MqttConfigResult parse(const std::string& p, std::string& k, std::string& v, bool on = true)
{
    return parseMqttConfig(p.data(), p.size(), on, k, v);
}

int main()
{
    std::string k, v;
    CHECK(parse("brightness=5", k, v) == MqttConfigResult::Ok && k == "brightness" && v == "5", "simple");
    CHECK(parse("message_a_text=a=b c", k, v) == MqttConfigResult::Ok && v == "a=b c", "value keeps '=' and spaces");
    CHECK(parse("night_start=", k, v) == MqttConfigResult::Ok && v.empty(), "empty value allowed");

    // the old 128-byte cut: a 300-byte value now arrives whole
    std::string longText(300, 'x');
    CHECK(parse("message_a_text=" + longText, k, v) == MqttConfigResult::Ok && v.size() == 300,
          "300-byte value kept whole (%zu)", v.size());
    std::string tooLong = "message_a_text=" + std::string(kMqttConfigMaxPayload, 'y');
    CHECK(parse(tooLong, k, v) == MqttConfigResult::TooLong, "over 512 bytes refused, not truncated");

    CHECK(parse("noequals", k, v) == MqttConfigResult::NoSeparator, "no '='");
    CHECK(parse("=value", k, v) == MqttConfigResult::NoSeparator, "empty key");
    CHECK(parse("#x=1", k, v) == MqttConfigResult::InvalidKey, "comment-looking key");
    CHECK(parse(std::string("a\nb=1"), k, v) == MqttConfigResult::InvalidKey, "key with newline");
    CHECK(parse("wifi_password=x", k, v) == MqttConfigResult::SecretKey, "secret refused");
    CHECK(parse("OW_API_KEY=x", k, v) == MqttConfigResult::SecretKey, "secret refused, any case");
    CHECK(parse("brightness=5", k, v, false) == MqttConfigResult::Disabled, "writes disabled");

    // payload buffers from MQTT are not NUL-terminated
    const char raw[] = {'a','=','1','G','A','R','B','A','G','E'};
    CHECK(parseMqttConfig(raw, 3, true, k, v) == MqttConfigResult::Ok && k == "a" && v == "1",
          "uses the given length only");
    CHECK(std::strlen(mqttConfigResultText(MqttConfigResult::TooLong)) > 0, "result text");

    if (failures) { printf("%d mqtt config check(s) FAILED\n", failures); return 1; }
    printf("all mqtt config checks passed\n");
    return 0;
}
