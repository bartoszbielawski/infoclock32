# Session notes — infoclock32 firmware structure

Handoff notes describing how the firmware is organized. Written 2026-09-16; line
numbers drift, so references are by file/function where possible.

## What this is

ESP32 Arduino/PlatformIO firmware for a 64×8 LED-matrix "info clock" (8 cascaded
MAX7219 modules, LEDMatrixDriver `explicit-spi` branch). Content sources: clock/date,
OpenWeatherMap, CERN LHC beam status, temperature sensors, custom messages with
date windows/countdowns, restaurant menu, sunrise/sunset, Game of Life idle bursts.
Exposed via web UI, MQTT, and a LittleFS `key=value` config file (`/config.txt`).

## Repo layout

```
platformio.ini          4 envs: esp32-c3-devkitm-1, esp32dev, esp32-s2-saola-1, esp32-s3-devkitc-1
platformio.local.ini    optional overrides (extra_configs; gitignored)
config.example.txt      annotated example of /config.txt
data/config.txt         default LittleFS image (pio run -t uploadfs)
src/                    ~24 .cpp: task bodies + HTTP handlers (see task table below)
include/                ~43 headers: stores, resource_manager, task_registry, sensor drivers
lib/                    empty (placeholder)
old_code/, disabled/    dead/excluded code (disabled/ = websocket "wrap" client) — do not edit
tests/host/             host-side checks, run via ./tests/host/run.sh (no hardware needed)
tests/host/prototype/   host prototype w/ FreeRTOS shim (std::thread/mutex/cv) — NOT built by run.sh,
                        but its shim must stay compatible with resource_manager.hpp
test/                   PlatformIO test dir (effectively unused)
```

## Boot sequence — `src/main.cpp` `setup()`

1. Serial (1 M baud; HWCDC waits ≤2 s for host), `LittleFS.begin(true)`
2. `DataStore::load_from_file("/config.txt")` — must precede WiFi auto-connect in `hardware_init()`
3. `hardware_init()` (src/hardware_init.cpp): board/network, WiFiManager, mDNS
4. SNTP (`configTime`, UTC base; `apply_timezone()` applies TZ from config)
5. SPI from `pins.hpp` (config overrides `spi_sck/spi_mosi/spi_cs`) → `LMDS` ctor (8 segments, 5 MHz)
6. `ResourceManager<LMDS>::initialize(lmds)` + `setPreReleaseHook(wipe_on_release)`
7. `logger_init()`, boot banner scroll (version + reset reason; `rst: wdt (<task>)` if RTC-memory culprit present; AP-setup hint in WiFi AP mode)
8. Task creation (table below) — most gated by `enable_*` config flags
9. Boot date check (waits ≤10 s for NTP)
10. `enableLoopWDT()` + `start_watchdog_task()` LAST — supervisor starts enforcing only once tasks are registered

`loop()` just blinks the LED (loopTask is a real FreeRTOS task; `enableLoopWDT()` watches it).

## Runtime architecture

### Tasks (created in main.cpp; all priority 1 unless noted)

| Task name | Function (file) | Purpose | Gate | WDT timeout |
|---|---|---|---|---|
| ClockTask | `displayClock` (clock_task.cpp) | always-on clock/date | always | 60 s |
| WeatherTask | `open_weather_map_task` (weather_forecast_task.cpp) | weather + forecast | `enable_weather` | set in file |
| LHCStatusTask | `lhc_status_task` (lhc_status_task.cpp) | LHC beam status | `enable_lhc` | set in file |
| MQTTTask | `mqtt_task` (mqtt_task.cpp) | MQTT client | `enable_mqtt` | 90 s |
| WebServerTask | `web_server_task` (http_server_task.cpp) | all HTTP routes (single task) | always | set in file |
| TempSensorTask | `temp_sensor_task` (temp_sensor_task.cpp) | reads sensor, RuntimeStore + display | always (sensor via `temp_sensor` key + factory) | set in file |
| CustomMessageTask | `custom_message_task` (custom_message_task.cpp) | message slots, date windows/countdowns | always | set in file |
| NightModeTask | `night_mode_task` (night_mode_task.cpp) | brightness by HH:MM window | always | 120 s |
| OTATask | `ota_task` (ota_task.cpp) | ArduinoOTA | `ota_password` set | — |
| RestoMenuTask | `resto_menu_task` (resto_menu_task.cpp) | restaurant menu fetch/scroll | `enable_resto` | set in file |
| SunTimesTask | `sun_times_task` (sun_times_task.cpp) | sunrise/sunset line | `enable_sun` + `sun_lat/sun_lon` | set in file |
| LifeTask | `game_of_life_task` (game_of_life_task.cpp) | GoL bursts between content | `enable_life` | 60 s |
| (ResourceManager) | `manager_task_function` | display grant/release loop | via `initialize()` | — |
| (watchdog) | `start_watchdog_task()` | supervisor | last in setup | — |

`screen_wipe_task.cpp` is NOT a task — `wipe_on_release()` is the manager's
pre-release hook (13 random effects, throttled by `wipe_interval`, 0 disables).

### Display arbitration — `include/resource_manager.hpp` (heart of scheduling)

- Singleton `ResourceManager<LMDS>`; two FreeRTOS queues of `DisplayRequest{task, atomic abandoned}`:
  `request_queue` (depth 3, normal) and `fast_queue` (depth 2, **priority lane**).
- `acquire(timeout = portMAX_DELAY, priority = false)` → RAII `ResourceGuard`
  (destructor → `release_access()`). `make_access_request()` queues the request and
  blocks on a task notification until the manager grants it or the timeout expires.
- Manager task loop: drain `fast_queue` first (0-tick), then `request_queue` (1 s poll);
  claim the request atomically (skip+retire if the owner already timed out), grant =
  notify requester, then wait for release — the wait is vetoed when the holder
  stops showing progress for 30 s (force handover; see renewHold below).
- Handshake (reworked 2026-09, see below): requests carry a `gen` number matched
  against a small pending-request registry (critical-section protected) so the
  manager never grants a request whose owner timed out while it sat queued — the
  old code granted those and stalled the display for the full 30 s force-handover
  (worst case: livelock under timeout-heavy contention). After a claim, the wait is
  bounded by notifications alone: a requester timing out post-claim swallows any
  late-delivered grant (100 ms window, prevents stale notifies leaking into the
  next acquire) and sends one give-up notification that ends the manager's wait.
  `current_task`/`drop_count_` are `std::atomic`.
- **Force-handover is progress-based**: holders call `renewHold()` every frame
  (scrollCanvas, Life burst) and the manager steals the display only after 30 s
  *without progress*. A fixed 30 s-from-grant timer was caught stealing the
  display mid-scroll (26 s scroll + wipe ≈ 30 s) during prototype web testing —
  two tasks would draw concurrently.
- `release_access()` runs `pre_release_hook` (wipe effect) while the holder still owns
  the display, then notifies the manager.
- Priority lane users: ClockTask and user pushes (web `/push`, `/actions` push, MQTT
  push). Everyone else queues normally.
- `drop_count_` is surfaced via `getDropCount()` on `/status`.

**Convention:** long-lived display tasks (clock, life, sun times) deliberately BLOCK
on acquire rather than timeout — a timed-out request can linger as a dead entry and
poison the handshake (comments in game_of_life_task.cpp / sun_times_task.cpp).
Interactive paths (web/MQTT) are the exception: short timeouts, see below.

### Watchdog — `include/task_registry.hpp` + `src/watchdog_task.cpp`

- Each task calls `registerTask(name, stack, timeoutMs)` as its first statement, then
  `task_heartbeat()` per loop iteration. Helpers: `task_heartbeat_grace(ms)`
  (planned long wait; only RAISES the allowance, cleared by next beat),
  `task_grace_all(ms)` (broadcast: caller is about to monopolize the display —
  Life burst uses `burst_s*1000 + 10000`), `task_mark_exited()`.
- Supervisor (hardware WDT watches it too): warn-once — first stale check logs `WDT`,
  second consecutive reboots. Culprit kept in RTC memory, shown in next boot banner
  and published to RuntimeStore as `wdt_culprit`.
- **Clock task pattern (added 2026-09):** `task_heartbeat()` →
  `task_heartbeat_grace(90000)` → `acquire(portMAX_DELAY, priority=true)` →
  `task_heartbeat()` + `task_heartbeat_grace(30000)` for the hold. Needed because
  `extend()` never lowers an allowance — a beat re-anchors the window after a long wait.

### Stores

- `DataStore` (data_store.hpp): singleton `std::map<string,string>` backed by
  `/config.txt` on LittleFS; `save_to_file()` preserves comments. **NOT mutex-protected**
  (known race — web/MQTT/tasks mutate concurrently).
- `RuntimeStore` (runtime_store.hpp): mutex-protected volatile KV — sensor values,
  display placeholders (`{temp_c}` etc.), `wdt_culprit`, `pressure_trend`.
- `DeviceStore` (device_store.hpp): WiFi/system info for MQTT `…/request`.

### Web UI — http_server_task.cpp + *_handler.cpp + web_ui.cpp

Single WebServerTask serves everything (routes registered in http_server_task.cpp
`web_server_task`, ~L536): dashboard, `/push` (unauthenticated by design),
`/actions` (actions_handler.cpp: push/brightness/nightmode/hostname/timezone/
reset_display/password), messages editor (messages_handler.cpp), OTA upload
(update_handler.cpp), WiFi (wifi_handler.cpp), `/status` (shows drop count),
`/reboot`. Auth = HTTP Basic (`is_authenticated()` in web_ui.cpp); the HTML
`/` and `/status` pages are intentionally public — `/api/*` and the editors are
gated. Because one task serves all routes, any blocking route freezes the whole
UI — mitigated by the short timeouts below, but keep new handlers non-blocking.
The host prototype now serves this same code on localhost:8080 via a WebServer
shim (see tests/host/prototype/README.md) — use it to exercise the web stack
without hardware.

### MQTT — mqtt_task.cpp

Topics prefixed with client id (`mqtt_client_id`, default `infoclock32`):
`/push` (queued, depth 4), `/looped`, `/clear`, `/brightness`, `/power`, `/config`,
`/reboot`, `/request` → answers on `/publish/<var>` (DeviceStore → RuntimeStore →
DataStore). Hardware commands are deferred to the task loop via pending flags.
**Gap:** `/config` sets values in memory but never persists them.

### Rendering — graphic_utils.cpp / graphic_utils.hpp

`scrollMessage(text, display, speed_ms)`, `scrollCanvas()`, and all wipe/transition
effects. Blocking per-column `display.display()` + `vTaskDelay` loops — a scroll
holds the display for seconds (a long message can hold ~15-30 s).

## Config system

`/config.txt` on LittleFS, `key=value` with comments preserved on save. Loaded once
in setup; runtime changes via web `/actions` (persisted) or MQTT `/config`
(NOT persisted). Keys documented in README (brightness, night_*, language, timezone,
enable_*, intervals, sensor type, SPI/I2C pins, mqtt_*, web_password, …).

## Build & test

- `pio` is NOT on PATH: use `~/.platformio/penv/bin/pio` (Core 6.1.19).
- Firmware build: `~/.platformio/penv/bin/pio run -e esp32dev` (~15 s warm).
  Last known: Flash 67.5%, RAM 17.9% on esp32dev. Pre-existing ArduinoJson
  `containsKey` deprecation warnings in resto_menu_task.cpp — ignore.
- Host tests: `./tests/host/run.sh` — compiles+runs custom_message, sun_times,
  life_step, weather_icons, pressure_trend, wdt checks (watchdog staleness math).
- LittleFS image: `pio run -t uploadfs` (data/config.txt). Monitor speeds set per env.

## Conventions

- Heavy singleton pattern: `DataStore/RuntimeStore/DeviceStore/ResourceManager/
  TaskRegistry::getInstance()`.
- Logging: `logPrintf("TAG", ...)` (include/logger.hpp) — SYS, DISP, WEB, MQT, RES,
  WDT, LIFE, SCR, SUN, NGT, …
- Task loops: `registerTask()` first, `task_heartbeat()` at loop top, grace before
  every planned long sleep (interval + margin), `task_grace_all` before monopolizing
  the display.
- Comments are extensive and explain *why* (race notes, watchdog math) — keep that style.

## Suggested improvements (from 2026-09 review; ordered by value/risk)

### Display scheduling

1. ✅ **Non-blocking interactive display access** — web/MQTT acquires now use short
   timeouts (2 s priority / 500 ms / 1 s); 503/"display busy" branches are reachable.
   **Prototype testing raised the HTTP push timeout to 11 s**: the clock holds the
   display ~7 s of every ~9 s cycle, so 2 s made dashboard pushes fail ~78% of the
   time; 11 s spans one full fast-lane cycle, and pushes are granted at the next
   release with near-certainty. Brightness/reset stay at 500 ms.
2. ✅ **Priority lane** — clock + user pushes on `fast_queue`; coalescing is inherent
   (blocking acquire ⇒ a task never has two queued requests).
3. ✅ **Clock watchdog grace** around acquire/hold (beat re-anchors the window).
4. ⬜ **Bound hold times at the source** (was #3):
   - Add a preemption checkpoint to `scrollMessage`/`scrollCanvas` (graphic_utils.cpp):
     every N columns, check a "yield requested" flag set by the manager when a
     priority-lane request is waiting; long scrolls then break early.
   - Cap Life burst duration and/or yield mid-burst on the same flag.
   - Make the pre-release wipe (screen_wipe_task.cpp) skip when the fast queue is
     non-empty instead of extending every hold; expose queue depth via a getter.
5. ✅ **Handshake rework** (was #4): `current_task`/`drop_count_` now atomic; the
   dead `abandoned` flag and the fragile 50 ms retract dance are gone, replaced by
   a gen-tagged pending registry (pre-grant skip + atomic claim) and a give-up
   notification channel (100 ms late-grant swallow + one notify) that bounds the
   manager's release wait without depending on owner-mutable registry state.
   Built `tests/host/prototype/glue/rm_stress.cpp` first (abandoned-request
   regression + 6-thread contention with exclusion/termination asserts) — it
   caught a real bug in the first attempt (sliced DEAD-check reading a clobbered
   registry entry → 30 s zombie waits) before it could ship. Wired into
   `tests/host/run.sh`.
6. ⬜ **Anti-starvation for the normal lane**: clock+push bursts can delay sensor
   content; if it ever matters, alternate one fast : one normal grant in the manager.
7. ⬜ **Observability**: expose current holder, fast/normal queue depths, and last
   force-handover on `/status` for debugging contention.

### Thread safety & persistence

8. ⬜ **DataStore locking** — plain `std::map` mutated from WebServerTask, MQTT
   callback, and display tasks; add a mutex (or portMUX critical sections) around
   `get_value`/`set_value`/`save_to_file`, and make `save_to_file` write to a temp
   file + rename so a power cut can't truncate `/config.txt`.
9. ⬜ **Persist MQTT `/config`** — currently memory-only and silently lost on reboot;
   debounce saves (LittleFS wear) or mark which keys persist.
10. ⬜ **HTTP client timeouts** — surface connect/read timeouts in `HttpUtils::httpGet`
    (include/http_utils.hpp); tasks currently block for the full TLS response.

### Task hygiene

11. ⬜ **Grace-before-acquire audit** — clock is fixed; verify weather/lhc/resto/
    temp_sensor/custom_message cover their blocking `acquire()` waits with
    `task_heartbeat_grace` (or rely on `task_grace_all` broadcasts).
12. ⬜ **NightModeTask** acquires the whole display just for `setIntensity()` — add a
    manager method that applies a brightness change on grant (no hold), or use a
    short timeout and retry.
13. ⬜ Minor: `graphic_utils.cpp` defines a vestigial global
    `ResourceManager<LMDS> displayManager` (second, unused instance — verify with
    grep, then remove); lhc_status_task.cpp ~L109 "wait a minute" comment over a
    300 s sleep; single-task WebServerTask means one slow route still stalls the UI
    (keep new handlers non-blocking).

### Security

14. ⬜ **`/push` authentication toggle** — currently unauthenticated by design; add a
    config key (e.g. `push_requires_auth`, default off) and/or a push rate limit so
    an open endpoint can't monopolize the display.

### Testing

15. ⬜ Build `tests/host/prototype/` in `run.sh` (or CI) so the FreeRTOS shim can't
    silently drift from `resource_manager.hpp`; it already has a hang-injection hook
    (`host_set_hang`) suitable for watchdog/wedge tests.

## Recent changes (2026-09 session)

- `include/resource_manager.hpp`: fast/priority queue, `acquire(timeout, priority)`,
  manager drains fast lane first; coalescing note.
- `src/clock_task.cpp`: priority lane + grace before/after acquire (beat re-anchors).
- `src/actions_handler.cpp`: `/push` + `/actions` push → 2 s priority; brightness/
  reset_display → 500 ms (503/"display busy" branches now reachable).
- `src/mqtt_task.cpp`: brightness 500 ms (defer + retry, logged once); MQTT push 10 s
  priority with re-queue-and-retry (drop only if push queue full); looped message 1 s
  skip-a-cycle.
- `README.md`: new "Display scheduling" section.
- **Handshake rework (#4)** in `include/resource_manager.hpp`: atomics; gen-tagged
  pending registry + atomic claim replaces the dead `abandoned` flag; give-up
  channel (100 ms late-grant swallow + notify) replaces the retract dance. Stress
  test `tests/host/prototype/glue/rm_stress.cpp` (build target `./build.sh
  rm_stress`), wired into `run.sh`; it reproduced the old 31.8 s abandoned-request
  stall and a livelock pre-fix, and caught a zombie-wait bug in the first rework
  attempt. Baseline check: grants = hook releases − 2 (phase-1 holders) on every run.
- **Host prototype HTTP server**: `WebServer` shim (BSD sockets, shim/WebServer.h +
  webserver_shim.cpp, plus mbedtls-base64/ESPmDNS/WiFi-scan shims), real
  `web_server_task` + handlers now run on the host on port 8080
  (`INFOCLOCK_HTTP_PORT` override); `/update` stubbed in glue/web_stub.cpp.
  String shim gained the device's writable `operator[]`; host_main now gates
  LifeTask on `enable_life` (device parity).
- **Push timeout 2 s → 11 s** (actions_handler.cpp, both sites): prototype testing
  showed the clock's ~9 s cycle (≈80% display duty) made 2 s pushes fail ~78% of
  the time; 11 s spans one full fast-lane cycle.
- **Force-handover made progress-based**: `renewHold()` on holders (scrollCanvas
  per frame, Life burst per generation) + manager steals only after 30 s without
  progress — a fixed 30 s timer was caught stealing a legitimate 30 s
  scroll+wipe hold mid-draw.
- Verified: `pio run -e esp32dev` SUCCESS; `./tests/host/run.sh` all pass (incl.
  stress); prototype curl suite — /api/status 401/401/200 auth matrix, /push 200 +
  display rendering, 503 while busy, /actions, /messages, /edit, /wifi, /update
  stub, 404.
