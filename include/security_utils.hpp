#pragma once

// Light-weight security helpers (pure C++, host-testable — see
// tests/host/security_utils_check.cpp). The project's threat model is a home
// LAN device, so these are deliberately small: constant-time compare, a
// failed-login throttle, an optional /push token, body size caps and a boot
// warning list for risky config.

#include <cstdint>
#include <string>
#include <vector>

// Compare without an early exit, so response time doesn't reveal how many
// leading characters of a password were right. Length still leaks (fine).
inline bool constantTimeEquals(const std::string& a, const std::string& b)
{
    unsigned char diff = (unsigned char)(a.size() != b.size());
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; i++)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

// Blocks after `maxFailures` bad logins inside `windowS`, for `lockS`.
// Global rather than per-IP (a single web task, tiny RAM): an attacker can
// lock the legitimate user out for a minute, which is acceptable here.
class AuthThrottle
{
public:
    explicit AuthThrottle(int maxFailures = 5, int windowS = 60, int lockS = 60)
        : max_(maxFailures), window_(windowS), lock_(lockS) {}

    bool blocked(int64_t now) const { return now < lockedUntil_; }

    // Seconds until attempts are accepted again (0 when not blocked).
    int retryAfter(int64_t now) const
    {
        return blocked(now) ? (int)(lockedUntil_ - now) : 0;
    }

    void recordFailure(int64_t now)
    {
        if (count_ == 0 || now - windowStart_ > window_)
        {
            windowStart_ = now;
            count_ = 0;
        }
        if (++count_ >= max_)
        {
            lockedUntil_ = now + lock_;
            count_ = 0;
        }
    }

    void recordSuccess() { count_ = 0; }

private:
    int max_, window_, lock_;
    int count_ = 0;
    int64_t windowStart_ = 0;
    int64_t lockedUntil_ = 0;
};

// /push is open unless `push_token` is set; then the request must carry it.
inline bool pushTokenOk(const std::string& configured, const std::string& supplied)
{
    if (configured.empty()) return true;
    return constantTimeEquals(configured, supplied);
}

// Upper bounds for POST bodies that end up in RAM and on flash.
inline constexpr size_t kMaxConfigBody   = 24 * 1024;   // /edit
inline constexpr size_t kMaxMessagesBody = 8 * 1024;    // /messages (sum of args)

inline bool bodyTooLarge(size_t length, size_t limit) { return length > limit; }

// Human-readable notes about risky configuration, logged once at boot.
// `Store` needs: std::string get_value(const std::string&, const std::string&).
template <class Store>
std::vector<std::string> securityWarnings(Store& ds)
{
    std::vector<std::string> w;
    auto get = [&ds](const char* k) { return ds.get_value(k, ""); };

    if (get("web_password").empty())
        w.push_back("web UI has no password (set web_password): anyone on the network can change settings");
    if (get("push_token").empty())
        w.push_back("/push is open (set push_token to require ?token=...)");

    const bool mqtt = !get("mqtt_server").empty();
    if (mqtt && get("mqtt_password").empty())
        w.push_back("MQTT broker configured without mqtt_password");
    if (mqtt && get("mqtt_config_writes") != "0")
        w.push_back("MQTT /config writes are enabled (set mqtt_config_writes=0 to disable)");
    return w;
}
