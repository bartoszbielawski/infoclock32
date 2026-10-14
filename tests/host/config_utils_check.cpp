// Host-side tests for include/config_utils.hpp.
#include <config_utils.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    // value sanitising: no value may produce a second config line
    CHECK(sanitizeConfigValue("a\nweb_password=x") == "a web_password=x", "LF folded");
    CHECK(sanitizeConfigValue("a\r\nb") == "a  b", "CRLF folded");
    CHECK(sanitizeConfigValue("plain=value") == "plain=value", "'=' in a value is fine");

    CHECK(isValidConfigKey("sensors") && isValidConfigKey("sim2_co2"), "normal keys");
    CHECK(!isValidConfigKey(""), "empty key");
    CHECK(!isValidConfigKey("#comment"), "comment-looking key");
    CHECK(!isValidConfigKey("a=b"), "key with '='");
    CHECK(!isValidConfigKey("a\nb"), "key with newline");

    // URL redaction
    CHECK(redactUrl("https://api.openweathermap.org/data/2.5/weather?id=1&appid=SECRET&units=metric")
              == "https://api.openweathermap.org/data/2.5/weather?id=1&appid=***&units=metric",
          "appid masked: %s", redactUrl("https://x/?id=1&appid=SECRET&units=metric").c_str());
    CHECK(redactUrl("http://h/p?api_key=K&token=T&q=1") == "http://h/p?api_key=***&token=***&q=1", "several secrets");
    CHECK(redactUrl("http://h/path") == "http://h/path", "no query untouched");
    CHECK(redactUrl("http://h/p?a=1&b=2") == "http://h/p?a=1&b=2", "plain query untouched");
    CHECK(redactUrl("http://h/p?appid=") == "http://h/p?appid=***", "empty secret still masked");

    if (failures) { printf("%d config util check(s) FAILED\n", failures); return 1; }
    printf("all config util checks passed\n");
    return 0;
}
