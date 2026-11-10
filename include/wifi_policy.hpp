#pragma once

// Decisions for Wi-Fi start-up and recovery (pure, host-tested in
// tests/host/wifi_policy_check.cpp; hardware_init.cpp does the I/O).
//
// Status values mirror Arduino's wl_status_t.
//
// Problem this fixes: when the configured network failed at boot for any
// reason - typically a power cut where the clock boots faster than the
// router - the driver's stored credentials were erased and the setup portal
// opened; after the portal timed out the monitor only called
// WiFi.reconnect(), which had no credentials left, so the clock stayed
// offline until rebooted.

namespace wifi_policy {

constexpr int kNoSsidAvail   = 1;   // WL_NO_SSID_AVAIL: network not visible
constexpr int kConnected     = 3;   // WL_CONNECTED
constexpr int kConnectFailed = 4;   // WL_CONNECT_FAILED: AP rejected us

// Only a rejection means the stored credentials are wrong. "Not visible yet"
// or a timeout is transient (router booting, out of range) and must not cost
// the credentials.
inline bool credentialsRejected(int status) { return status == kConnectFailed; }

// What boot does after the config credentials did not connect.
struct BootFailurePlan
{
    bool eraseDriverCache;   // wipe the pair the Wi-Fi driver keeps in flash
    bool openPortal;         // block in the WiFiManager setup portal
};

inline BootFailurePlan onBootFailure(bool haveConfigCredentials, int status)
{
    if (!haveConfigCredentials)
        return {false, true};           // nothing configured: portal (tries NVS first)
    if (credentialsRejected(status))
        return {true, true};            // wrong password: let the user fix it now
    // Transient: keep everything, continue offline; the monitor retries with
    // the config credentials and raises the fallback AP after wifi_ap_after_s.
    return {false, false};
}

// How the monitor retries while offline.
enum class Retry { BeginWithConfig, ReconnectDriver };

inline Retry retryMethod(bool haveConfigCredentials)
{
    // WiFi.reconnect() reuses whatever the driver holds, which may have been
    // erased; the config pair is the source of truth whenever it exists.
    return haveConfigCredentials ? Retry::BeginWithConfig : Retry::ReconnectDriver;
}

} // namespace wifi_policy
