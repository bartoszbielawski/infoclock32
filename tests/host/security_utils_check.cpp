// Host-side tests for include/security_utils.hpp.
#include <security_utils.hpp>
#include <cstdio>
#include <map>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

struct FakeStore {
    std::map<std::string, std::string> m;
    std::string get_value(const std::string& k, const std::string& d) {
        auto it = m.find(k); return it == m.end() ? d : it->second;
    }
};

int main()
{
    // constant-time compare: same answers as ==
    CHECK(constantTimeEquals("", ""), "empty equals empty");
    CHECK(constantTimeEquals("hunter2", "hunter2"), "equal");
    CHECK(!constantTimeEquals("hunter2", "hunter3"), "last char differs");
    CHECK(!constantTimeEquals("hunter2", "Hunter2"), "first char differs");
    CHECK(!constantTimeEquals("hunter", "hunter2"), "prefix is not equal");
    CHECK(!constantTimeEquals("", "x"), "empty vs non-empty");

    // throttle
    {
        AuthThrottle t(5, 60, 60);
        CHECK(!t.blocked(0), "starts open");
        for (int i = 0; i < 4; i++) t.recordFailure(i);
        CHECK(!t.blocked(10), "4 failures: still open");
        t.recordFailure(10);
        CHECK(t.blocked(11), "5th failure locks");
        CHECK(t.retryAfter(11) == 59, "retry-after %d", t.retryAfter(11));
        CHECK(!t.blocked(71), "unlocks after 60 s");
        // failures spread outside the window don't accumulate
        AuthThrottle u(3, 10, 60);
        u.recordFailure(0); u.recordFailure(5); u.recordFailure(20);
        CHECK(!u.blocked(21), "old failures expire");
        // success resets
        AuthThrottle v(3, 60, 60);
        v.recordFailure(0); v.recordFailure(1); v.recordSuccess(); v.recordFailure(2);
        CHECK(!v.blocked(3), "success resets the count");
        // relock after expiry works again
        AuthThrottle w(2, 60, 30);
        w.recordFailure(0); w.recordFailure(1);
        CHECK(w.blocked(2) && !w.blocked(40), "lock then expiry");
        w.recordFailure(41); w.recordFailure(42);
        CHECK(w.blocked(43), "locks again");
    }

    // push token
    CHECK(pushTokenOk("", ""), "no token configured: open");
    CHECK(pushTokenOk("", "anything"), "no token configured: any value ok");
    CHECK(pushTokenOk("s3cret", "s3cret"), "matching token");
    CHECK(!pushTokenOk("s3cret", ""), "missing token refused");
    CHECK(!pushTokenOk("s3cret", "wrong"), "wrong token refused");

    // body caps
    CHECK(!bodyTooLarge(kMaxConfigBody, kMaxConfigBody), "limit itself is allowed");
    CHECK(bodyTooLarge(kMaxConfigBody + 1, kMaxConfigBody), "over limit refused");

    // boot warnings
    {
        FakeStore open;
        auto w = securityWarnings(open);
        CHECK(w.size() == 2, "defaults: web password + push open (%zu)", w.size());
        FakeStore mq; mq.m["mqtt_server"] = "broker";
        CHECK(securityWarnings(mq).size() == 4, "mqtt adds password + config-writes warnings (%zu)",
              securityWarnings(mq).size());
        FakeStore safe;
        safe.m["web_password"] = "x"; safe.m["push_token"] = "y";
        safe.m["mqtt_server"] = "b"; safe.m["mqtt_password"] = "z"; safe.m["mqtt_config_writes"] = "0";
        CHECK(securityWarnings(safe).empty(), "hardened config: no warnings");
    }

    if (failures) { printf("%d security util check(s) FAILED\n", failures); return 1; }
    printf("all security util checks passed\n");
    return 0;
}
