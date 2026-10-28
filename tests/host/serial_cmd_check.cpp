// Host-side tests for include/serial_cmd.hpp.
#include <serial_cmd.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    auto c = parseSerialCommand("set wifi_ssid=fake_wifi\r\n");
    CHECK(c.cmd == SerialCmd::Set && c.key == "wifi_ssid" && c.value == "fake_wifi", "basic set");

    c = parseSerialCommand("set msg1_text=Wi-Fi {ssid} = ok");
    CHECK(c.cmd == SerialCmd::Set && c.key == "msg1_text" && c.value == "Wi-Fi {ssid} = ok", "value keeps spaces and '='");

    c = parseSerialCommand("set wifi_password=");
    CHECK(c.cmd == SerialCmd::Set && c.value.empty(), "empty value allowed");

    CHECK(parseSerialCommand("set =x").cmd == SerialCmd::Unknown, "empty key rejected");
    CHECK(parseSerialCommand("set novalue").cmd == SerialCmd::Unknown, "set without '=' rejected");

    c = parseSerialCommand("  get   hostname ");
    CHECK(c.cmd == SerialCmd::Get && c.key == "hostname", "get trims");
    CHECK(parseSerialCommand("get").cmd == SerialCmd::Unknown, "get without key");

    CHECK(parseSerialCommand("reboot").cmd == SerialCmd::Reboot, "reboot");
    CHECK(parseSerialCommand("wifi_forget").cmd == SerialCmd::WifiForget, "wifi_forget");
    CHECK(parseSerialCommand("wifi_scan").cmd == SerialCmd::WifiScan, "wifi_scan");
    c = parseSerialCommand("wifi_cache my net=pw");
    CHECK(c.cmd == SerialCmd::WifiCache && c.key == "my net" && c.value == "pw", "wifi_cache");
    CHECK(parseSerialCommand("wifi_cache nopass").cmd == SerialCmd::Unknown, "wifi_cache needs '='");
    CHECK(parseSerialCommand("help").cmd == SerialCmd::Help, "help");
    CHECK(parseSerialCommand("").cmd == SerialCmd::None, "empty line");
    CHECK(parseSerialCommand("   \r\n").cmd == SerialCmd::None, "blank line");
    CHECK(parseSerialCommand("rm -rf").cmd == SerialCmd::Unknown, "unknown verb");

    if (failures) { printf("%d serial cmd check(s) FAILED\n", failures); return 1; }
    printf("all serial cmd checks passed\n");
    return 0;
}
