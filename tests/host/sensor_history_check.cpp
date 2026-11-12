// Host-side tests for include/sensor_history.hpp.
#include <cstdio>
#include <cstring>
#include <string>
#include <sensor_history.hpp>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static std::string json(const HistorySample& s, int f)
{
    char b[16];
    history_value_json(s, f, b, sizeof(b));
    return b;
}

int main()
{
    // quantisation and validity flags
    HistorySample s = make_history_sample(1000, 21.46f, 48.6f, 937.24f, 612);
    CHECK(s.flags == (HF_TEMP | HF_RH | HF_HPA | HF_CO2), "all fields valid: %u", s.flags);
    CHECK(s.temp10 == 215 && s.rh == 49 && s.hpa10 == 9372 && s.co2 == 612,
          "quantised %d %u %u %u", s.temp10, s.rh, s.hpa10, s.co2);
    CHECK(json(s, 0) == "21.5" && json(s, 1) == "49" && json(s, 2) == "937.2" && json(s, 3) == "612",
          "json %s %s %s %s", json(s,0).c_str(), json(s,1).c_str(), json(s,2).c_str(), json(s,3).c_str());

    HistorySample neg = make_history_sample(1000, -12.34f, NAN, NAN, 0);
    CHECK(neg.flags == HF_TEMP && neg.temp10 == -123, "negative temp only: %u %d", neg.flags, neg.temp10);
    CHECK(json(neg, 0) == "-12.3" && json(neg, 1) == "null" && json(neg, 2) == "null" && json(neg, 3) == "null",
          "missing fields are null");
    CHECK(make_history_sample(1, 500.0f, 120.0f, 5.0f, -3).flags == 0, "out of range dropped");

    // bucketing: one sample per 300 s bucket, first reading wins
    SensorHistory h;
    CHECK(h.offer(make_history_sample(600, 20, 50, 1000, 0), 300), "first stored");
    CHECK(!h.offer(make_history_sample(899, 25, 50, 1000, 0), 300), "same bucket ignored");
    CHECK(h.offer(make_history_sample(900, 21, 50, 1000, 0), 300), "next bucket stored");
    CHECK(!h.offer(make_history_sample(700, 22, 50, 1000, 0), 300), "clock went back: ignored");
    CHECK(!h.offer(HistorySample{}, 300), "empty sample ignored");
    CHECK(!h.offer(make_history_sample(5000, 20, 50, 1000, 0), 0), "step 0 ignored");
    CHECK(h.size() == 2 && h.at(0).temp10 == 200 && h.at(1).temp10 == 210, "order oldest first");

    // wrap-around keeps the newest kCapacity samples, oldest first
    h.clear();
    const size_t N = SensorHistory::kCapacity + 10;
    for (size_t i = 0; i < N; i++)
        h.offer(make_history_sample((uint32_t)(i * 300), (float)i / 10.0f, NAN, NAN, 0), 300);
    CHECK(h.size() == SensorHistory::kCapacity, "capped at %zu", h.size());
    CHECK(h.at(0).t == 10 * 300, "oldest after wrap: %u", h.at(0).t);
    CHECK(h.at(SensorHistory::kCapacity - 1).t == (N - 1) * 300, "newest after wrap");
    for (size_t i = 1; i < h.size(); i++)
        CHECK(h.at(i).t > h.at(i - 1).t, "monotonic at %zu", i);

    CHECK(sizeof(HistorySample) <= 12, "sample is %zu bytes", sizeof(HistorySample));

    if (failures) { printf("%d sensor history check(s) FAILED\n", failures); return 1; }
    printf("all sensor history checks passed\n");
    return 0;
}
