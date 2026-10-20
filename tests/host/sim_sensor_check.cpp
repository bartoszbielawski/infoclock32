// Host-side tests for include/sim_sensor.hpp and include/sensor_round.hpp:
// the virtual sensor itself, and the full poll -> merge -> stale cycle driven
// by a virtual clock (no FreeRTOS, no hardware).
#include <sim_sensor.hpp>
#include <sensor_round.hpp>
#include <cstdio>
#include <map>
#include <string>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

// Minimal DataStore look-alike for simParamsFromStore().
struct FakeStore {
    std::map<std::string, std::string> m;
    bool has_key(const std::string& k) const { return m.count(k) > 0; }
    template <class T> T get_value(const std::string& k, T def) const {
        auto it = m.find(k);
        if (it == m.end()) return def;
        return (T)atof(it->second.c_str());
    }
};

static const SensorOffsets kNoOffsets;

// Drive a round with a virtual clock for `seconds`, stepping when due like
// the task does. Returns accepted-mask history per measurement via counters.
struct Run {
    int polls = 0, oks = 0;
    int accepted[MEAS_COUNT] = {};
    int dropped[MEAS_COUNT] = {};
};
static Run drive(SensorRound& r, int64_t from, int64_t to, int interval)
{
    Run run;
    for (int64_t t = from; t <= to; t++)
    {
        auto res = r.step(t, interval, kNoOffsets);
        if (res.polled) run.polls++;
        if (res.ok) run.oks++;
        for (int m = 0; m < MEAS_COUNT; m++)
        {
            if (res.accepted & measurementBit((Measurement)m)) run.accepted[m]++;
            if (res.dropped & measurementBit((Measurement)m)) run.dropped[m]++;
        }
    }
    return run;
}

int main()
{
    // ── type / config ─────────────────────────────────────────────────────
    CHECK(isSimType("sim") && isSimType("sim2") && isSimType("sim17"), "sim types");
    CHECK(!isSimType("simple") && !isSimType("si") && !isSimType("bme280") && !isSimType("sim2x"),
          "non-sim types rejected");
    {
        FakeStore ds;
        ds.m["sim2_temp"] = "19.5"; ds.m["sim2_co2"] = "800";
        ds.m["sim2_fail_every"] = "4"; ds.m["sim2_begin_fail"] = "1";
        SimParams p = simParamsFromStore(ds, "sim2");
        CHECK(p.temp == 19.5f && p.hasCo2 && p.co2 == 800, "params read with type prefix");
        CHECK(!p.hasHumidity && !p.hasPressure, "capabilities need their key");
        CHECK(p.failEvery == 4 && p.beginFails, "failure keys read");
        SimParams q = simParamsFromStore(ds, "sim");   // different prefix: defaults
        CHECK(q.temp == 21.0f && !q.hasCo2, "other prefix untouched");
    }

    // ── SimSensor behaviour ───────────────────────────────────────────────
    {
        SimParams p; p.hasPressure = true; p.drift = 2.0f; p.driftPeriod = 4;
        SimSensor s(p);
        CHECK(s.begin() && s.hasPressure() && !s.hasHumidity() && !s.hasCo2(), "capabilities");
        CHECK(s.read() && s.temperature() == 21.0f, "first read at phase 0");
        s.read();                                    // quarter period: peak
        CHECK(s.temperature() > 22.9f && s.temperature() < 23.1f, "drift peak %.2f", s.temperature());
        SimSensor t(p);                              // deterministic
        t.read(); t.read();
        CHECK(t.temperature() == s.temperature(), "deterministic");
    }
    {
        SimParams p; p.failEvery = 3;
        SimSensor s(p);
        bool r[6];
        for (int i = 0; i < 6; i++) r[i] = s.read();
        CHECK(r[0] && r[1] && !r[2] && r[3] && r[4] && !r[5], "every 3rd attempt fails");
    }
    {
        SimParams p; p.failAfter = 2; p.failCount = 3;
        SimSensor s(p);
        bool r[7];
        for (int i = 0; i < 7; i++) r[i] = s.read();
        CHECK(r[0] && r[1] && !r[2] && !r[3] && !r[4] && r[5] && r[6],
              "dies after 2 good reads for 3 attempts, then recovers");
    }
    {
        SimParams p; p.failAfter = 1; // forever
        SimSensor s(p);
        s.read();
        bool any = false;
        for (int i = 0; i < 50; i++) any = any || s.read();
        CHECK(!any, "stays dead when fail_count is -1");
        SimParams b; b.beginFails = true;
        CHECK(!SimSensor(b).begin(), "begin_fail");
    }

    // ── SensorRound with a virtual clock ──────────────────────────────────
    const int interval = 60;
    {   // healthy pair: one read per interval, round-robin, first provider wins
        SimParams a; a.temp = 20.0f; a.hasHumidity = true;
        SimParams b; b.temp = 25.0f; b.hasCo2 = true;
        SimSensor sa(a), sb(b);
        std::vector<Sensor*> l = { &sa, &sb };
        SensorRound r(l);
        Run run = drive(r, 0, 599, interval);
        CHECK(run.polls == 10 && run.oks == 10, "10 polls in 600 s at 60 s (%d)", run.polls);
        CHECK(sa.attempts() == 5 && sb.attempts() == 5, "alternates (%d/%d)", sa.attempts(), sb.attempts());
        CHECK(r.data.temp == 20.0f, "temperature stays with first sensor (%.1f)", r.data.temp);
        CHECK(r.data.have(MEAS_CO2) && r.data.have(MEAS_HUMIDITY), "union of capabilities");
        CHECK(run.accepted[MEAS_TEMP] == 5, "second sensor never overwrites temp (%d)", run.accepted[MEAS_TEMP]);
        CHECK(run.dropped[MEAS_TEMP] == 0, "healthy round drops nothing");
    }
    {   // first sensor dies for good: second takes over, nothing blanks early
        SimParams a; a.temp = 20.0f; a.failAfter = 3;
        SimParams b; b.temp = 25.0f;
        SimSensor sa(a), sb(b);
        std::vector<Sensor*> l = { &sa, &sb };
        SensorRound r(l);
        drive(r, 0, 3600, interval);
        CHECK(r.data.temp == 25.0f && r.data.slots[MEAS_TEMP].source == 1,
              "second sensor took over temp (%.1f src %d)", r.data.temp, r.data.slots[MEAS_TEMP].source);
        CHECK(r.data.have(MEAS_TEMP), "temp available after failover");
    }
    {   // lone sensor dies: reading is dropped after the stale window, once
        SimParams a; a.failAfter = 2;
        SimSensor sa(a);
        std::vector<Sensor*> l = { &sa };
        SensorRound r(l);
        Run run = drive(r, 0, 120, interval);       // two good reads at t=0,60
        CHECK(r.data.have(MEAS_TEMP), "still shown shortly after death");
        run = drive(r, 121, 60 + 120 + 5, interval);
        CHECK(r.data.have(MEAS_TEMP), "within the stale window of the last read");
        run = drive(r, 60 + 120 + 6, 3000, interval);
        CHECK(!r.data.have(MEAS_TEMP), "dropped after stale window");
        CHECK(run.dropped[MEAS_TEMP] == 1, "drop reported exactly once (%d)", run.dropped[MEAS_TEMP]);
    }
    {   // intermittent failures never blank the reading
        SimParams a; a.failEvery = 2;
        SimSensor sa(a);
        std::vector<Sensor*> l = { &sa };
        SensorRound r(l);
        Run run = drive(r, 0, 7200, interval);
        CHECK(run.dropped[MEAS_TEMP] == 0 && r.data.have(MEAS_TEMP), "flaky sensor stays available");
        CHECK(run.polls > run.oks, "some polls failed (%d/%d)", run.oks, run.polls);
    }
    {   // failed read retries sooner than a full interval
        SimParams a; a.failAfter = 0; a.failCount = 1;   // first attempt fails
        SimSensor sa(a);
        std::vector<Sensor*> l = { &sa };
        SensorRound r(l);
        auto res = r.step(0, 300, kNoOffsets);
        CHECK(res.polled && !res.ok, "first poll fails");
        CHECK(r.nextPollAt() == 30, "retry in 30 s, not 300 (%lld)", (long long)r.nextPollAt());
        res = r.step(30, 300, kNoOffsets);
        CHECK(res.ok && r.nextPollAt() == 330, "recovers and resumes the interval");
    }
    {   // offsets flow through, raw pressure stays raw
        SimParams a; a.hasPressure = true;
        SimSensor sa(a);
        std::vector<Sensor*> l = { &sa };
        SensorRound r(l);
        SensorOffsets off; off.temp = 1.0f; off.pressure = -3.0f;
        r.step(0, 60, off);
        CHECK(r.data.temp == 22.0f && r.data.pressure == 1010.0f && r.data.rawPressure == 1013.0f,
              "offsets applied (%.1f %.1f %.1f)", r.data.temp, r.data.pressure, r.data.rawPressure);
    }

    // ── light sensor beside a slow sensor ────────────────────────────────
    {
        FakeStore ds;
        ds.m["sim_temp"] = "20"; ds.m["sim_humidity"] = "40";
        ds.m["sim2_lux"] = "300"; ds.m["sim2_no_temp"] = "1"; ds.m["sim2_poll_s"] = "2";
        SimParams slow = simParamsFromStore(ds, "sim");
        SimParams lite = simParamsFromStore(ds, "sim2");
        CHECK(lite.hasLux && lite.lux == 300.0f && !lite.hasTemp && lite.pollS == 2, "lux params read");

        std::vector<Sensor*> l = { new SimSensor(lite, "Light"), new SimSensor(slow) };
        SensorRound r(l);
        CHECK(r.data.owner[MEAS_TEMP] == 1, "light sensor never owns temperature (owner %d)", r.data.owner[MEAS_TEMP]);
        CHECK(r.data.owner[MEAS_LUX] == 0, "light sensor owns lux");

        Run run = drive(r, 0, 599, 300);   // slow interval 300 s, light every 2 s
        CHECK(run.accepted[MEAS_LUX] >= 295 && run.accepted[MEAS_LUX] <= 300,
              "lux refreshed every ~2 s (%d)", run.accepted[MEAS_LUX]);
        CHECK(run.accepted[MEAS_TEMP] == 2, "slow sensor still polled at its interval (%d)", run.accepted[MEAS_TEMP]);
        CHECK(run.accepted[MEAS_HUMIDITY] == 2, "humidity from the slow sensor (%d)", run.accepted[MEAS_HUMIDITY]);
        CHECK(r.data.lux > 250.0f && r.data.lux < 350.0f, "lux value merged (%.1f)", r.data.lux);
        for (auto* s : l) delete s;
    }
    {   // only a light sensor: round still runs and wakes on its own timer
        SimParams lite; lite.hasLux = true; lite.hasTemp = false; lite.pollS = 2;
        std::vector<Sensor*> l = { new SimSensor(lite) };
        SensorRound r(l);
        auto a = r.step(0, 300, kNoOffsets);
        CHECK(a.polled && a.ok && a.accepted == measurementBit(MEAS_LUX), "lux-only first poll");
        CHECK(r.nextPollAt() == 2, "next poll on the sensor's own timer (%lld)", (long long)r.nextPollAt());
        auto b = r.step(1, 300, kNoOffsets);
        CHECK(!b.polled, "not polled before its period");
        delete l[0];
    }

    if (failures) { printf("%d sim sensor check(s) FAILED\n", failures); return 1; }
    printf("all sim sensor checks passed\n");
    return 0;
}
