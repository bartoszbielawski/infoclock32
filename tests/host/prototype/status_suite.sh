#!/bin/bash
# Black-box test of the timed status over HTTP. Works against the host prototype
# (start it with INFOCLOCK_HTTP_PORT=8080 and a config with status_1_text=TIMEFLASH,
# status_1_time=1, status_1_minutes=3 for the TIMEFLASH=1 part) or a real device.
# Note: inside WSL the wall clock can run slow against the monotonic clock, so
# the timing checks are approximate there.
# Usage: suite.sh <base-url> [token] [long-run-seconds]
# Env: TIMEFLASH=1 expects preset 1 = text TIMEFLASH, time=1 (prototype config).
H=$1; TOKEN=$2; LONG=${3:-0}
Q=""; [ -n "$TOKEN" ] && Q="&token=$TOKEN"
pass=0; fail=0
ok()  { echo "ok   $1"; pass=$((pass+1)); }
no()  { echo "FAIL $1 :: $2"; fail=$((fail+1)); }
eq()  { if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "got '$2' want '$3'"; fi; }
has() { case "$2" in *"$3"*) ok "$1";; *) no "$1" "got '$2' want contains '$3'";; esac; }
j()   { curl -s -m 20 "$@"; }
info(){ j "$H/status/info?x=1$Q"; }
frame(){ j "$H/frame"; }
set_(){ j "$H/status/set?$1$Q"; }
cancel(){ j "$H/status/cancel?x=1$Q"; }
now() { date +%s; }
wait_inactive() { # $1 = max seconds; prints seconds waited, rc 1 on timeout
  local t0=$(now)
  while true; do
    case "$(info)" in *'"active":false'*) echo $(( $(now) - t0 )); return 0;; esac
    [ $(( $(now) - t0 )) -ge "$1" ] && { echo $(( $(now) - t0 )); return 1; }
    sleep 1
  done
}
wait_active() { local t0=$(now); while true; do case "$(info)" in *'"active":true'*) echo $(( $(now) - t0 )); return 0;; esac; [ $(( $(now)-t0 )) -ge "$1" ] && return 1; sleep 0.5; done; }
# Sample the frame every 0.5 s for 2.5 s: a once-per-second cue must show at least
# two different frames. (Two samples exactly 1 s apart can land on the same phase.)
alternates() {
  local first cur i; first=$(frame)
  for i in 1 2 3 4 5; do sleep 0.5; cur=$(frame); [ "$cur" != "$first" ] && { ok "$1"; return; }; done
  no "$1" "frame never changed in 2.5 s"
}
# Device uptime in seconds from /api/status ("[Nd ]Nh Nm Ns").
uptime_s() {
  j "$H/api/status" | sed -n 's/.*"uptime":"\([^"]*\)".*/\1/p' |
    awk '{s=0; for(i=1;i<=NF;i++){v=$i+0; if($i~/d$/)s+=v*86400; else if($i~/h$/)s+=v*3600; else if($i~/m$/)s+=v*60; else if($i~/s$/)s+=v}; print s}'
}
borders() { local f; f=$(frame); local bad=0 l; while IFS= read -r l; do [ "${l:0:1}" = "#" ] && [ "${l: -1}" = "#" ] || bad=1; done <<<"$f"; echo $bad; }

echo "== reachability"
until info | grep -q active; do sleep 2; done
cancel >/dev/null; wait_inactive 40 >/dev/null
eq "idle: inactive" "$(info)" '{"active":false}'

echo "== input validation"
c() { curl -s -o /dev/null -m 15 -w '%{http_code}' "$H/status/set?$1$Q"; }
eq "no params -> 400"          "$(c "")" 400
eq "unknown preset -> 404"     "$(c "preset=99")" 404
eq "bad minutes -> 400"        "$(c "text=x&minutes=abc")" 400
eq "zero minutes -> 400"       "$(c "text=x&minutes=0")" 400
eq "721 minutes -> 400"        "$(c "text=x&minutes=721")" 400
eq "blank text -> 400"         "$(c "text=%20%20")" 400
eq "still idle after bad input" "$(info)" '{"active":false}'

echo "== start latency and basic state"
t0=$(now); r=$(set_ "text=HELLO&cue=none&minutes=1"); has "set ok" "$r" '"ok":true'
w=$(wait_active 5); ok "reported active (${w}s)"
sleep 3
has "info text" "$(info)" '"text":"HELLO"'
f1=$(frame); sleep 1.2; f2=$(frame)
eq "static text does not change" "$f1" "$f2"
has "frame is not the clock face (HELLO has H at col)" "$(echo "$f1" | head -1)" "#"

echo "== replace restarts timer and text"
set_ "text=AAA&cue=none&minutes=30" >/dev/null; sleep 1
set_ "text=BBB&cue=none&minutes=60" >/dev/null; sleep 2
i=$(info); has "replaced text" "$i" '"text":"BBB"'; has "replaced total 3600" "$i" '"total_s":3600'

echo "== cues"
set_ "text=X&cue=border&minutes=2" >/dev/null; sleep 3
eq "border bars on both ends" "$(borders)" 0
f1=$(frame); sleep 1.3; f2=$(frame); eq "border is steady" "$f1" "$f2"
set_ "text=X&cue=blink&minutes=2" >/dev/null; sleep 3
alternates "blink changes second to second"
set_ "text=X&cue=invert&minutes=2" >/dev/null; sleep 3
alternates "invert changes second to second"

echo "== end of hour"
r=$(set_ "text=EOH&minutes=eoh"); has "eoh accepted" "$r" '"ok":true'
rem=$(info | sed -n 's/.*"remaining_s":\([0-9]*\).*/\1/p'); [ "$rem" -gt 0 ] && [ "$rem" -le 7200 ] && ok "eoh remaining sane ($rem s)" || no "eoh" "remaining=$rem"

echo "== push and actions fail fast during a status"
t0=$(now); r=$(j "$H/push?msg=hi$Q"); d=$(( $(now)-t0 ))
has "push refused" "$r" "status active"; [ $d -le 2 ] && ok "push answered in ${d}s" || no "push latency" "${d}s"
t0=$(now); cancel >/dev/null; w=$(wait_inactive 30); ok "cancel took ${w}s to clear"

echo "== long text scrolls, cancel latency"
# %20, not raw spaces: curl rejects a URL with spaces before sending it, and
# this check then only passed when something else changed the frame.
r=$(set_ "text=THIS%20IS%20A%20MUCH%20TOO%20LONG%20STATUS%20TEXT%20FOR%20THE%20MATRIX&cue=none&minutes=2")
has "long text accepted" "$r" '"ok":true'; sleep 4
f1=$(frame); sleep 0.8; f2=$(frame); [ "$f1" != "$f2" ] && ok "long text scrolls" || no "scroll" "frames equal"
t0=$(now); cancel >/dev/null; w=$(wait_inactive 30); [ $? -eq 0 ] && ok "scroll cancel cleared in ${w}s" || no "scroll cancel" "still active after ${w}s"

echo "== rotation resumes after cancel"
sleep 6
f1=$(frame); sleep 2.2; f2=$(frame)
[ -n "$f1" ] && ok "display responsive after status"
has "no status after cancel" "$(info)" '"active":false'

echo "== web page"
p=$(j "$H/s?x=1$Q"); has "page lists ON AIR preset button or custom form" "$p" "Custom text"
has "page has duration chips" "$p" "Until end of hour"
set_ "text=PAGE&minutes=1" >/dev/null; sleep 2
p=$(j "$H/s?x=1$Q"); has "page shows active banner" "$p" "Showing now"; has "page shows cancel" "$p" "Cancel"
code=$(curl -s -o /dev/null -m 15 -w '%{http_code} %{redirect_url}' -X POST -d "ui=1&text=UI&minutes=1${TOKEN:+&token=$TOKEN}" "$H/status/set")
has "ui post redirects" "$code" "303"
cancel >/dev/null; wait_inactive 30 >/dev/null

echo "== hammer: rapid set/cancel/push/api"
for i in $(seq 1 25); do
  set_ "text=S$i&cue=blink&minutes=1" >/dev/null
  [ $((i%3)) -eq 0 ] && cancel >/dev/null
  [ $((i%4)) -eq 0 ] && j "$H/push?msg=x$Q" >/dev/null
  j "$H/api/status" >/dev/null
done
cancel >/dev/null; w=$(wait_inactive 40); [ $? -eq 0 ] && ok "hammer: settled inactive (${w}s)" || no "hammer" "still active"
code=$(curl -s -o /dev/null -m 15 -w '%{http_code}' "$H/api/status"); eq "web server responsive after hammer" "$code" 200

echo "== natural expiry (60 s status)"
t0=$(now); set_ "text=EXPIRE&cue=none&minutes=1" >/dev/null
w=$(wait_inactive 90); rc=$?; [ $rc -eq 0 ] && [ "$w" -ge 55 ] && [ "$w" -le 75 ] && ok "expired by itself after ${w}s" || no "expiry" "took ${w}s rc=$rc"

if [ -n "$TIMEFLASH" ]; then
  echo "== periodic time (preset 1, time=1 min, 3 min status)"
  set_ "preset=1&minutes=3" >/dev/null; sleep 5
  base=$(frame); sleep 50; before=$(frame)
  eq "text frame stable before the time flash" "$base" "$before"
  sleep 8; tf=$(frame); [ "$tf" != "$before" ] && ok "time shown at ~60 s" || no "time flash" "frame unchanged at 63 s"
  sleep 5; after=$(frame); eq "back to the text after 3 s" "$before" "$after"
  cancel >/dev/null; wait_inactive 30 >/dev/null
fi

if [ "$LONG" -gt 0 ]; then
  echo "== long hold ($LONG s): watchdog grace and liveness"
  up0=$(uptime_s)
  set_ "text=LONG&cue=blink&minutes=$(( (LONG+59)/60 ))" >/dev/null
  t0=$(now)
  # (Stop checking 30 s early: the status ends at ceil(LONG/60) minutes, and a
  # slow wall clock, as in WSL, would otherwise make the last check race it.)
  while [ $(( $(now)-t0 )) -lt $(( LONG - 30 )) ]; do
    sleep 20
    code=$(curl -s -o /dev/null -m 15 -w '%{http_code}' "$H/api/status")
    [ "$code" = 200 ] || no "long hold: web server" "http $code at $(( $(now)-t0 )) s"
    has "long hold: still active at $(( $(now)-t0 )) s" "$(info)" '"active":true'
  done
  up1=$(uptime_s)
  [ -n "$up0" ] && [ -n "$up1" ] && [ "$up1" -ge $(( up0 + LONG - 30 )) ] && ok "no reboot during the hold (uptime ${up0}s -> ${up1}s)" || no "reboot during the hold?" "uptime $up0 -> $up1"
  w=$(wait_inactive $(( (LONG+59)/60*60 - LONG + 30 ))); [ $? -eq 0 ] && ok "long hold ended by itself" || no "long hold end" "still active"
  sleep 15
  has "web server alive after long hold" "$(j "$H/api/status")" "{"
fi

echo
echo "RESULT: $pass passed, $fail failed"
[ $fail -eq 0 ]
