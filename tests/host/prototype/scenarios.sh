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

WORK=$(mktemp -d)
if ! ./build.sh prototype > "$WORK/build.log" 2>&1; then
    echo "FAIL: prototype build (last 60 lines of compiler output)"
    tail -n 60 "$WORK/build.log"
    rm -rf "$WORK"
    exit 1
fi

PIDS=""
# Stop the prototypes and wait until they are gone, so a run started right
# after this one doesn't find its ports still taken (SIGKILL after 5 s).
cleanup() {
    for p in $PIDS; do kill $p 2>/dev/null; done
    i=0
    while [ $i -lt 50 ]; do
        alive=0
        for p in $PIDS; do kill -0 $p 2>/dev/null && alive=1; done
        [ $alive -eq 0 ] && break
        sleep 0.1; i=$((i + 1))
    done
    for p in $PIDS; do kill -9 $p 2>/dev/null; done
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

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

# Positive checks poll (a loaded CI runner can be slow to reach steady
# state); absence checks run once after the settle sleep.
expect_metric() {   # case metric value
    i=0
    while :; do
        v=$(metrics "$1" | awk -v m="infoclock_$2" '$1==m {print $2}')
        [ "$v" = "$3" ] && break
        i=$((i + 1)); [ $i -ge 30 ] && break
        sleep 2
    done
    if [ "$v" = "$3" ]; then ok "$1: $2 == $3"; else fail "$1: $2 is '${v:-absent}', want $3"; fi
}
expect_no_metric() {   # case metric
    v=$(metrics "$1" | awk -v m="infoclock_$2" '$1==m {print $2}')
    if [ -z "$v" ]; then ok "$1: $2 absent"; else fail "$1: $2 is '$v', want absent"; fi
}
expect_log() {   # case pattern
    i=0
    until grep -aq -- "$2" "$WORK/$1.log"; do
        i=$((i + 1)); [ $i -ge 30 ] && break
        sleep 2
    done
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
# two-phase sensor (like a DS18B20): started, then collected a second later
start conv      "sensors=sim,sim2" "sim_temp=19" "sim_conversion_ms=750" "sim2_temp=25" "sim2_co2=700"
# offsets are applied at write time
start offsets    "sensors=sim" "sim_temp=20" "sim_humidity=40" "temp_offset=1.5" "humidity_offset=-5" "log_display=1"

# web: runtime JSON stays valid, /edit cannot be broken out of via a value,
# secrets never reach /api/runtime
start web  "sensors=sim" "sim_temp=20" "message_hello_text=</textarea><b>x" "ow_api_key=TOPSECRET"

# weather: the real task streams the canned OpenWeatherMap responses
start weather "sensors=stub" "enable_weather=1"

# lhc: the real task streams the canned LHC Page 1 JSON
start lhc "sensors=stub" "enable_lhc=1"

# menu: the real task parses the canned Novae menu through the JSON filter
start resto "sensors=stub" "enable_resto=1" "resto_test_date=2026-01-01"

# xss: hostile config values (as MQTT /config could set them) must come out
# escaped on every page; a {secret} placeholder must never expand
start xss  "sensors=stub" "message_hello_text=x' onfocus='alert(1)" \
    "message_hello_start=2026-01-01' onfocus='a" "night_start=12:00' autofocus onfocus='b" \
    "night_end=13:00\"><script>alert(2)</script>" "timezone=UTC0'><script>alert(3)</script>" \
    "mqtt_server=<b>evil</b>" "message_leak_text=pw {wifi_password} end" "wifi_password=TOPSECRETPW"

# sec: web password + push token, throttle, size cap, boot warnings
start sec  "sensors=stub" "web_password=pw" "push_token=tok"
start open "sensors=stub"

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

expect_metric    conv temp_c   19
expect_metric    conv temp_co2 700
if grep -aq "failed to read from sensor 'Sim'" "$WORK/conv.log"; then fail "conv: a two-phase read failed"; else ok "conv: no failed two-phase reads"; fi
expect_metric    offsets temp_c  21.5
expect_metric    offsets temp_rh 35

eval "p=\$PORT_web"
if curl -s --max-time 3 "http://127.0.0.1:$p/api/runtime" | python3 -c 'import json,sys; json.load(sys.stdin)' 2>/dev/null
then ok "web: /api/runtime is valid JSON"; else fail "web: /api/runtime is not valid JSON"; fi
page=$(curl -s --max-time 3 "http://127.0.0.1:$p/edit")
case "$page" in
    *"&lt;/textarea&gt;"*) ok "web: /edit escapes </textarea>" ;;
    *) fail "web: /edit does not escape </textarea>" ;;
esac
# /metrics must not be cut short (the system block was once truncated at 191 B)
if metrics web | grep -q 'infoclock_display_queue_depth{lane="normal"}'; then
    ok "web: /metrics complete"
else
    fail "web: /metrics truncated ($(metrics web | wc -c | tr -d ' ') bytes)"
fi
if grep -aq "TOPSECRET" "$WORK/web.log"; then fail "web: secret value found in log"; else ok "web: secret value not logged"; fi

# 000 = no HTTP response at all. On a loaded machine the prototype's simple
# socket shim occasionally drops a request before any handler sees it (the
# prototype log then has no trace of it), so retry once; any real status code,
# including errors, is returned as is.
code() {
    c=$(curl -s -o /dev/null -w '%{http_code}' --max-time "${MAXT:-15}" "$@")
    if [ "$c" = 000 ]; then
        c=$(curl -s -o /dev/null -w '%{http_code}' --max-time "${MAXT:-15}" "$@")
    fi
    echo "$c"
}
expect_code() {   # label want got
    if [ "$3" = "$2" ]; then ok "$1: HTTP $2"; else fail "$1: HTTP $3, want $2"; fi
}
eval "p=\$PORT_sec"; U="http://127.0.0.1:$p"

if grep -aq "web UI has no password" "$WORK/sec.log"; then fail "sec: warns about a missing password although one is set"; else ok "sec: no password warning when set"; fi
expect_log open "web UI has no password"
expect_log open "/push is open"

expect_code "push without token"    403 "$(code "$U/push?msg=hi")"
expect_code "push with wrong token" 403 "$(code "$U/push?msg=hi&token=nope")"
pc=000
for i in 1 2 3; do
    pc=$(MAXT=20 code "$U/push?msg=hi&token=tok")
    [ "$pc" = 200 ] && break
done
expect_code "push with token" 200 "$pc"
expect_code "page without login"    401 "$(code "$U/edit")"
expect_code "page with password"    200 "$(code -u u:pw "$U/edit")"
big=$(head -c 30000 /dev/zero | tr '\0' 'a')
expect_code "oversize /edit POST"   413 "$(code -u u:pw --data-urlencode "content=$big" "$U/edit")"
# a body the device's WebServer could not parse arrives with "content" empty:
# that must be 413 (too large) as well, not 400, and the config stays intact
c0=$(curl -s -m 5 -u u:pw "$U/edit" | wc -c | tr -d ' ')
expect_code "oversize body, empty content" 413 "$(code -u u:pw --data-urlencode "content=" --data-urlencode "pad=$big" "$U/edit")"
c1=$(curl -s -m 5 -u u:pw "$U/edit" | wc -c | tr -d ' ')
if [ "$c0" = "$c1" ]; then ok "config intact after oversize empty POST"; else fail "config changed ($c0 -> $c1)"; fi
# Send bad logins until the lock-out kicks in (a loaded runner can drop a
# request client-side, so don't assume exactly five are counted).
# An empty /edit body must never wipe the config (a large POST the device's
# WebServer fails to parse arrives as an empty "content").
before=$(curl -s -m 5 -u u:pw "$U/edit" | wc -c | tr -d ' ')
expect_code "empty /edit POST refused" 400 "$(code -u u:pw --data-urlencode "content=" "$U/edit")"
after=$(curl -s -m 5 -u u:pw "$U/edit" | wc -c | tr -d ' ')
if [ "$before" = "$after" ] && [ "$before" -gt 800 ]; then ok "config intact after empty POST"; else fail "config changed by empty POST ($before -> $after)"; fi

expect_log weather "\[WTH\] Prototype: 21.4"
expect_log weather "18.7.*light rain"
if grep -aq "failed to parse temperature\|missing city name" "$WORK/weather.log"; then fail "weather: parse failed"; else ok "weather: both responses parsed"; fi
expect_log lhc "\[LHC\] status: PROTON PHYSICS: STABLE BEAMS @ 6800 GeV"
expect_log lhc "\[LHC\] comment: Stable beams - fill 10000 .* Let's go!"
if grep -aq "old comment\|SETUP\|JSON error" "$WORK/lhc.log"; then fail "lhc: stale entry or JSON error"; else ok "lhc: latest entries parsed"; fi
expect_log resto "R3: Roasted chicken with rosemary potatoes"
expect_log resto "R3: Mushroom risotto with parmesan and grilled zucchini"
if grep -aq "Evening dish" "$WORK/resto.log"; then fail "resto: non-midi dish shown"; else ok "resto: only midi dishes"; fi
if grep -aq "R3: \*\*MENU TABLE" "$WORK/resto.log"; then fail "resto: header listed as a dish"; else ok "resto: header not listed as a dish"; fi
expect_log resto "skipped header '\*\*MENU TABLE DU CHEF CE VENDREDI\*\*'"
if grep -aq "JSON error" "$WORK/resto.log"; then fail "resto: JSON error"; else ok "resto: menu parsed without errors"; fi

# display lines stay out of /log by default (Serial still has them);
# log_display=1 keeps them
eval "pw=\$PORT_web"; eval "po=\$PORT_offsets"
if curl -s --max-time 15 "http://127.0.0.1:$pw/log/entries" | grep -q '"tag":"DISP"'; then fail "log: DISP lines in /log by default"; else ok "log: no DISP lines in /log by default"; fi
if grep -aq "\[DISP\]" "$WORK/web.log"; then ok "log: DISP lines still on Serial"; else fail "log: DISP lines missing on Serial"; fi
if curl -s --max-time 15 "http://127.0.0.1:$po/log/entries" | grep -q '"tag":"DISP"'; then ok "log: log_display=1 keeps DISP lines"; else fail "log: log_display=1 has no DISP lines"; fi

eval "px=\$PORT_xss"; X="http://127.0.0.1:$px"
pages=$(for u in / /messages /actions /status; do curl -s --max-time 15 "$X$u"; done)
for raw in "onfocus='" "<script>alert" "<b>evil" "12:00' autofocus" "2026-01-01' onfocus"; do
    if printf '%s' "$pages" | grep -qF -- "$raw"; then fail "xss: raw '$raw' reached a page"; else ok "xss: '$raw' escaped"; fi
done
if printf '%s' "$pages" | grep -qF "x&#39; onfocus=&#39;alert(1)"; then ok "xss: message text shown escaped"; else fail "xss: escaped message text not found"; fi
if printf '%s' "$pages" | grep -qF "12:00&#39; autofocus onfocus=&#39;b"; then ok "xss: night start shown escaped"; else fail "xss: escaped night start not found"; fi
if printf '%s' "$pages" | grep -qF "TOPSECRETPW"; then fail "xss: secret placeholder expanded on a page"; else ok "xss: {wifi_password} not expanded on pages"; fi
sleep 2
if grep -aqF "TOPSECRETPW" "$WORK/xss.log"; then fail "xss: secret placeholder expanded into the log"; else ok "xss: secret not in the log"; fi

# CSRF: a browser POST from another site is refused, same-site works
expect_code "cross-site POST /reboot"      403 "$(code -u u:pw -X POST -H 'Origin: https://evil.example' "$U/reboot")"
expect_code "cross-site POST via Referer"  403 "$(code -u u:pw -H 'Referer: https://evil.example/x' --data-urlencode "content=" "$U/edit")"
expect_code "same-site POST /edit (empty)" 400 "$(code -u u:pw -H "Origin: http://127.0.0.1:$p" --data-urlencode "content=" "$U/edit")"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
    [ "$(code -u u:wrong "$U/edit")" = 429 ] && break
done
expect_code "throttled after 5 bad logins" 429 "$(code -u u:pw "$U/edit")"

if [ $SLOW -eq 1 ]; then
    echo "── slow cases (waiting for the stale window) ──"
    sleep 135
    expect_no_metric dead temp_c
    expect_log       dead "stale"
    expect_metric    failover temp_c 25
    expect_metric    flaky temp_c 20
fi

if [ $FAILED -eq 0 ]; then
    echo "all scenario checks passed"
else
    # Show what each prototype printed so a CI failure is diagnosable.
    for f in "$WORK"/*.log; do
        echo "── $(basename "$f") (last 25 lines) ──"
        tail -n 25 "$f"
    done
    echo "$FAILED scenario check(s) FAILED"
    exit 1
fi
