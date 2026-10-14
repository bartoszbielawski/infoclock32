#!/bin/sh
# Scenario tests for the sensor task, run against the real firmware task code
# in the host prototype with virtual (sim) sensors. Asserts on the log output
# and on the prototype's HTTP /metrics.
#
#   ./scenarios.sh          fast cases (~1 min)
#   ./scenarios.sh --slow   also failover / stale-drop cases (~3 min: the
#                           stale window has a 120 s floor)
set -u
cd "$(dirname "$0")"

SLOW=0
[ "${1:-}" = "--slow" ] && SLOW=1

./build.sh prototype >/dev/null 2>&1 || { echo "FAIL: prototype build"; exit 1; }

WORK=$(mktemp -d)
PIDS=""
trap 'for p in $PIDS; do kill $p 2>/dev/null; done; rm -rf "$WORK"' EXIT INT TERM

BASE=$(grep -vE '^(sensors|sensor_interval|sensor_display|sensor_hold_s|sensor_readout_interval|sim[0-9]*_[a-z0-9_]+|enable_[a-z]+|msg_interval|mqtt_server)=' fs/config.txt)
PORT=18080
N=0

# start <name> <config lines...>   (each case: own fs dir, own HTTP port)
start() {
    name=$1; shift
    N=$((N + 1))
    mkdir -p "$WORK/$name"
    {
        echo "$BASE"
        echo "enable_weather=0"; echo "enable_lhc=0"; echo "enable_sun=0"
        echo "enable_life=0"; echo "enable_resto=0"; echo "enable_mqtt=0"
        echo "sensor_interval=5"
        echo "sensor_display=temp,humidity,pressure,co2"
        for l in "$@"; do echo "$l"; done
    } > "$WORK/$name/config.txt"
    INFOCLOCK_HTTP_PORT=$((PORT + N)) ./prototype --offline --static --fs "$WORK/$name" \
        > "$WORK/$name.log" 2>&1 &
    PIDS="$PIDS $!"
    eval "PORT_$name=$((PORT + N))"
}

metrics() { eval "p=\$PORT_$1"; curl -s --max-time 3 "http://127.0.0.1:$p/metrics"; }

FAILED=0
ok()   { echo "ok   $1"; }
fail() { echo "FAIL $1"; FAILED=$((FAILED + 1)); }

expect_metric() {   # case metric value
    v=$(metrics "$1" | awk -v m="infoclock_$2" '$1==m {print $2}')
    if [ "$v" = "$3" ]; then ok "$1: $2 == $3"; else fail "$1: $2 is '${v:-absent}', want $3"; fi
}
expect_no_metric() {   # case metric
    v=$(metrics "$1" | awk -v m="infoclock_$2" '$1==m {print $2}')
    if [ -z "$v" ]; then ok "$1: $2 absent"; else fail "$1: $2 is '$v', want absent"; fi
}
expect_log() {   # case pattern
    if grep -aq -- "$2" "$WORK/$1.log"; then ok "$1: log has '$2'"; else fail "$1: log lacks '$2'"; fi
}

# ── cases ────────────────────────────────────────────────────────────────
# merge: first listed provider owns temp/humidity, second adds CO2
start merge  "sensors=sim,sim2" \
    "sim_temp=20" "sim_humidity=40" "sim2_temp=25" "sim2_humidity=60" "sim2_co2=800"
# a sensor whose begin() fails is dropped; the other keeps working
start beginfail  "sensors=sim,sim2" \
    "sim_temp=20" "sim2_temp=25" "sim2_co2=800" "sim2_begin_fail=1"
# only a stub: task exits, nothing is published
start stubonly   "sensors=stub"
# offsets are applied at write time
start offsets    "sensors=sim" "sim_temp=20" "sim_humidity=40" "temp_offset=1.5" "humidity_offset=-5"

if [ $SLOW -eq 1 ]; then
    # lone sensor dies after 2 good reads: reading is dropped after the stale window
    start dead       "sensors=sim" "sim_temp=20" "sim_fail_after=2"
    # first sensor dies: the second takes over temperature
    start failover   "sensors=sim,sim2" "sim_temp=20" "sim_fail_after=2" "sim2_temp=25"
    # intermittent failures never blank the reading
    start flaky      "sensors=sim" "sim_temp=20" "sim_fail_every=2"
fi

sleep 50

echo "── fast cases ──"
expect_metric    merge temp_c   20
expect_metric    merge temp_rh  40
expect_metric    merge temp_co2 800
expect_log       merge "starting with 2 sensor(s)"

expect_log       beginfail "failed to initialize"
expect_log       beginfail "starting with 1 sensor(s)"
expect_metric    beginfail temp_c 20
expect_no_metric beginfail temp_co2

expect_log       stubonly "task exiting"
expect_no_metric stubonly temp_c

expect_metric    offsets temp_c  21.5
expect_metric    offsets temp_rh 35

if [ $SLOW -eq 1 ]; then
    echo "── slow cases (waiting for the stale window) ──"
    sleep 135
    expect_no_metric dead temp_c
    expect_log       dead "stale"
    expect_metric    failover temp_c 25
    expect_metric    flaky temp_c 20
fi

if [ $FAILED -eq 0 ]; then echo "all scenario checks passed"; else echo "$FAILED scenario check(s) FAILED"; exit 1; fi
