// Host-side tests for include/sensor_merge.hpp: init pruning, first-provider
// ownership, staleness and wake scheduling for the multi-sensor round.
#include <sensor_merge.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static int alive = 0;   // live FakeSensor count, to verify pruning deletes

struct FakeSensor : Sensor {
    bool h, p, c, stub;
    explicit FakeSensor(bool h = false, bool p = false, bool c = false, bool stub = false)
        : h(h), p(p), c(c), stub(stub) { alive++; }
    ~FakeSensor() override { alive--; }
    bool begin() override { return true; }
    bool read() override { return true; }
    float temperature() const override { return 20.0f; }
    bool hasHumidity() const override { return h; }
    bool hasPressure() const override { return p; }
    bool hasCo2() const override { return c; }
    bool isStub() const override { return stub; }
    const char* name() const override { return "fake"; }
};

int main()
{
    // ── pruneSensors ──────────────────────────────────────────────────────
    {   // failed init is dropped and deleted
        std::vector<Sensor*> l = { new FakeSensor(), new FakeSensor(), new FakeSensor() };
        pruneSensors(l, {true, false, true});
        CHECK(l.size() == 2 && alive == 2, "failed begin pruned (size %zu alive %d)", l.size(), alive);
        for (auto* s : l) delete s;
        CHECK(alive == 0, "no leak");
    }
    {   // lone stub: list emptied so the task exits
        std::vector<Sensor*> l = { new FakeSensor(false, false, false, true) };
        pruneSensors(l, {true});
        CHECK(l.empty() && alive == 0, "lone stub removed");
    }
    {   // stub next to a real sensor is dropped, real kept
        std::vector<Sensor*> l = { new FakeSensor(false, false, false, true), new FakeSensor(true) };
        pruneSensors(l, {true, true});
        CHECK(l.size() == 1 && !l[0]->isStub(), "stub dropped beside real sensor");
        for (auto* s : l) delete s;
    }
    {   // real sensor failed, stub ok: nothing usable
        std::vector<Sensor*> l = { new FakeSensor(true), new FakeSensor(false, false, false, true) };
        pruneSensors(l, {false, true});
        CHECK(l.empty() && alive == 0, "failed real + stub -> empty");
    }

    // ── firstProvider ─────────────────────────────────────────────────────
    {
        FakeSensor bme(true, true), scd(true, false, true);
        std::vector<Sensor*> l = { &bme, &scd };
        CHECK(firstProvider(l, MEAS_TEMP) == 0, "temp owner is first listed");
        CHECK(firstProvider(l, MEAS_HUMIDITY) == 0, "humidity owner is first listed");
        CHECK(firstProvider(l, MEAS_PRESSURE) == 0, "pressure owner");
        CHECK(firstProvider(l, MEAS_CO2) == 1, "co2 owner is the only provider");
        FakeSensor none;
        std::vector<Sensor*> n = { &none };
        CHECK(firstProvider(n, MEAS_CO2) == -1, "no provider -> -1");
    }

    // ── slotAccepts / staleness ───────────────────────────────────────────
    {
        const int64_t stale = staleAfterSeconds(2, 300);   // 1800
        CHECK(stale == 1800, "stale window %lld", (long long)stale);
        CHECK(staleAfterSeconds(1, 5) == 120, "stale floor 120 s");

        MeasurementSlot s;
        CHECK(slotAccepts(s, 1, 0, 0, stale), "empty slot accepts non-owner");

        s.have = true; s.source = 0; s.stamp = 100;
        CHECK(slotAccepts(s, 0, 0, 200, stale), "owner always accepted");
        CHECK(!slotAccepts(s, 1, 0, 200, stale), "non-owner blocked while owner fresh");
        CHECK(slotAccepts(s, 1, 0, 100 + stale + 1, stale), "non-owner takes over when owner stale");

        s.source = 1; s.stamp = 5000;   // fallback now holds the value
        CHECK(slotAccepts(s, 1, 0, 5300, stale), "fallback keeps updating its own slot");
        CHECK(!slotAccepts(s, 2, 0, 5300, stale), "third sensor blocked");
        CHECK(slotAccepts(s, 0, 0, 5300, stale), "recovered owner reclaims");

        s.have = true; s.stamp = 0;
        CHECK(!slotStale(s, stale, stale), "exactly at window is not stale");
        CHECK(slotStale(s, stale + 1, stale), "past window is stale");
        MeasurementSlot e;
        CHECK(!slotStale(e, 99999, stale) && !slotFresh(e, 0, stale), "empty slot is neither stale nor fresh");
    }

    // ── wakeDelayMs ───────────────────────────────────────────────────────
    CHECK(wakeDelayMs(300, -1) == 30000, "capped at 30 s");
    CHECK(wakeDelayMs(5, -1) == 5000, "poll due in 5 s (not quantised to 30)");
    CHECK(wakeDelayMs(0, -1) == 1000, "already due: min 1 s");
    CHECK(wakeDelayMs(-7, -1) == 1000, "overdue: min 1 s");
    CHECK(wakeDelayMs(300, 12) == 12000, "scroll sooner than poll");
    CHECK(wakeDelayMs(8, 12) == 8000, "poll sooner than scroll");

    // ── MeasurementSet: round-robin merge with scripted sensors ───────────
    {
        struct Scripted : FakeSensor {
            float t, hum, press; int co2v;
            Scripted(float t, bool h, float hum, bool p, float press, bool c, int co2v)
                : FakeSensor(h, p, c), t(t), hum(hum), press(press), co2v(co2v) {}
            float temperature() const override { return t; }
            float humidity() const override { return hum; }
            float pressure() const override { return press; }
            int16_t co2Concentration() const override { return (int16_t)co2v; }
        };
        Scripted bme(21.0f, true, 40.0f, true, 1013.0f, false, 0);
        Scripted scd(23.0f, true, 55.0f, false, 0.0f, true, 800);
        std::vector<Sensor*> l = { &bme, &scd };

        MeasurementSet d;
        d.setOwners(l);
        CHECK(d.owner[MEAS_TEMP] == 0 && d.owner[MEAS_CO2] == 1, "owners resolved");

        SensorOffsets off; off.temp = 0.5f; off.pressure = -2.0f; off.co2 = -1000.0f;
        const int64_t stale = staleAfterSeconds(2, 300);

        unsigned got = d.ingest(bme, 0, 0, stale, off);
        CHECK(got == (measurementBit(MEAS_TEMP) | measurementBit(MEAS_HUMIDITY) | measurementBit(MEAS_PRESSURE)),
              "bme provides temp/hum/press (mask %u)", got);
        CHECK(d.temp == 21.5f && d.pressure == 1011.0f && d.rawPressure == 1013.0f,
              "offsets applied, raw pressure kept (%.1f %.1f %.1f)", d.temp, d.pressure, d.rawPressure);

        got = d.ingest(scd, 1, 300, stale, off);
        CHECK(got == measurementBit(MEAS_CO2), "scd fills only CO2 while bme is fresh (mask %u)", got);
        CHECK(d.humidity == 40.0f && d.temp == 21.5f, "temp/humidity stay with first provider");
        CHECK(d.co2 == 0, "negative co2 clamped to 0 (%d)", d.co2);

        // bme goes quiet; scd keeps reading and takes over after the window
        got = d.ingest(scd, 1, 900, stale, off);
        CHECK(!(got & measurementBit(MEAS_TEMP)), "scd must not take temp while bme still fresh");
        got = d.ingest(scd, 1, 1900, stale, off);
        CHECK((got & measurementBit(MEAS_TEMP)) && d.temp == 23.5f, "scd takes over stale temp (%.1f)", d.temp);
        CHECK(d.slots[MEAS_TEMP].source == 1, "source recorded");
        got = d.ingest(scd, 1, 2200, stale, off);
        CHECK(got & measurementBit(MEAS_TEMP), "fallback keeps updating its own slot");

        // bme recovers and reclaims ownership immediately
        got = d.ingest(bme, 0, 2500, stale, off);
        CHECK((got & measurementBit(MEAS_TEMP)) && d.temp == 21.5f, "owner reclaims");

        // everything goes quiet -> swept
        unsigned dropped = d.sweepStale(2500 + stale + 1, stale);
        CHECK(dropped == (measurementBit(MEAS_TEMP) | measurementBit(MEAS_HUMIDITY) |
                          measurementBit(MEAS_PRESSURE) | measurementBit(MEAS_CO2)),
              "stale sweep drops readings (mask %u)", dropped);
        CHECK(!d.have(MEAS_TEMP) && !d.have(MEAS_PRESSURE), "dropped readings unavailable");
        CHECK(d.sweepStale(2500 + stale + 2, stale) == 0, "sweep reports each drop once");
    }

    // ── light sensors ─────────────────────────────────────────────────────
    {
        struct Lite : Sensor {
            bool begin() override { return true; }
            bool read() override { return true; }
            float temperature() const override { return 0.0f; }
            bool hasTemperature() const override { return false; }
            bool hasLux() const override { return true; }
            float lux() const override { return 123.5f; }
            const char* name() const override { return "lite"; }
        } lite;
        FakeSensor bme(true);
        CHECK(!sensorProvides(lite, MEAS_TEMP) && sensorProvides(lite, MEAS_LUX), "light sensor: lux, no temp");
        CHECK(sensorProvides(bme, MEAS_TEMP) && !sensorProvides(bme, MEAS_LUX), "regular sensor: temp, no lux");

        std::vector<Sensor*> l = { &lite, &bme };
        CHECK(firstProvider(l, MEAS_TEMP) == 1, "temp owner skips the light sensor");
        CHECK(firstProvider(l, MEAS_LUX) == 0, "lux owner");

        MeasurementSet d; d.setOwners(l);
        SensorOffsets off;
        unsigned got = d.ingest(lite, 0, 100, 120, off);
        CHECK(got == measurementBit(MEAS_LUX) && d.lux == 123.5f && !d.have(MEAS_TEMP),
              "light sensor fills only lux (mask %u)", got);
    }

    if (failures) { printf("%d sensor merge check(s) FAILED\n", failures); return 1; }
    printf("all sensor merge checks passed\n");
    return 0;
}
