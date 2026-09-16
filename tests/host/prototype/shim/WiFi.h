#pragma once
// Host shim: WiFi station with a fake "always configured" state machine.
#include "WString.h"
#include "Client.h"

enum wl_status_t
{
    WL_IDLE_STATUS,
    WL_NO_SSID_AVAIL,
    WL_CONNECTED,
    WL_CONNECT_FAILED,
    WL_DISCONNECTED
};

// Scan status / auth mode constants (device-compatible values).
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED  (-2)
enum wifi_auth_mode_t
{
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK
};

class WiFiClass
{
public:
    int status() const { return WL_CONNECTED; }

    IPAddress localIP() const { return IPAddress(192, 168, 7, 42); }
    IPAddress gatewayIP() const { return IPAddress(192, 168, 7, 1); }
    String SSID() const { return String("prototype-net"); }
    String macAddress() const { return String("02:00:00:00:00:42"); }
    int RSSI() const { return -55; }
    int channel() const { return 6; }

    // Wi-Fi scan — canned results; there is no radio on the host.
    int scanNetworks(bool async = false) { (void)async; return 2; }
    int scanComplete() { return 2; }
    String SSID(int i) const { return i == 0 ? String("prototype-net") : String("neighbour-5g"); }
    int    RSSI(int) const { return -55; }
    int    encryptionType(int) const { return WIFI_AUTH_WPA2_PSK; }

    const char* getHostname() const { return hostname_.c_str(); }
    void setHostname(const char* hostname) { hostname_ = hostname ? hostname : ""; }

    void begin(const char*, const char*) {}
    void reconnect() {}

private:
    std::string hostname_ = "infoclock32";
};

extern WiFiClass WiFi;

// The real ESP32 WiFi.h also declares the client types; firmware sources
// (mqtt_task) rely on that transitive include.
#include "WiFiClient.h"
