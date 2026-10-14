// Host-side tests for include/secret_keys.hpp.
#include <secret_keys.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    const char* secret[] = { "wifi_password", "ota_password", "web_password", "mqtt_password",
                             "ow_api_key", "novae_key", "MQTT_PASSWORD", "some_secret", "api_token", "key" };
    const char* open[]   = { "sensors", "sensor_interval", "hostname", "mqtt_server", "mqtt_user",
                             "ow_city_id", "keyboard_layout", "brightness", "monkey", "wifi_ssid", "sun_lat" };
    for (const char* k : secret) CHECK(isSecretKey(k), "'%s' must be secret", k);
    for (const char* k : open)   CHECK(!isSecretKey(k), "'%s' must not be secret", k);

    CHECK(!strcmp(loggableValue("wifi_password", "hunter2"), "***"), "secret masked");
    CHECK(!strcmp(loggableValue("wifi_password", ""), "(empty)"), "empty secret shown as empty");
    CHECK(!strcmp(loggableValue("sensors", "bme280"), "bme280"), "plain value untouched");

    if (failures) { printf("%d secret key check(s) FAILED\n", failures); return 1; }
    printf("all secret key checks passed\n");
    return 0;
}
