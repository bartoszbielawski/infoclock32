// Host-side tests for include/wifi_policy.hpp.
#include <wifi_policy.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

using namespace wifi_policy;

int main()
{
    // router still booting / out of range: keep credentials, no portal
    auto p = onBootFailure(true, kNoSsidAvail);
    CHECK(!p.eraseDriverCache && !p.openPortal, "network not visible is transient");
    p = onBootFailure(true, 6 /* WL_DISCONNECTED, e.g. 10 s timeout */);
    CHECK(!p.eraseDriverCache && !p.openPortal, "timeout is transient");
    p = onBootFailure(true, 0 /* WL_IDLE_STATUS, still trying */);
    CHECK(!p.eraseDriverCache && !p.openPortal, "still connecting is transient");

    // the AP rejected the credentials: erase the stale cache and open the portal
    p = onBootFailure(true, kConnectFailed);
    CHECK(p.eraseDriverCache && p.openPortal, "rejected credentials -> erase + portal");

    // nothing configured: portal, but never erase (the NVS pair is the
    // recovery hatch for a freshly reflashed filesystem)
    p = onBootFailure(false, kNoSsidAvail);
    CHECK(!p.eraseDriverCache && p.openPortal, "no config credentials -> portal only");

    CHECK(credentialsRejected(kConnectFailed) && !credentialsRejected(kNoSsidAvail) &&
          !credentialsRejected(kConnected), "only WL_CONNECT_FAILED is a rejection");

    // retries use the config pair whenever there is one
    CHECK(retryMethod(true) == Retry::BeginWithConfig, "config credentials -> WiFi.begin");
    CHECK(retryMethod(false) == Retry::ReconnectDriver, "no config -> WiFi.reconnect");

    if (failures) { printf("%d wifi policy check(s) FAILED\n", failures); return 1; }
    printf("all wifi policy checks passed\n");
    return 0;
}
