# Plan: Multi-sensor support (infoclock32)

Status: **IN PROGRESS** (implementation ~90% complete; remaining work listed below).
Last updated: 2026-10-14

## Objective
Allow multiple sensors to run concurrently via round-robin polling, consolidating
results into the existing flat RuntimeStore keys, replacing the single-sensor
`temp_*` config scheme with a new `sensors`/`sensor_*` config surface.

## Requirements
- Support multiple sensors simultaneously via a comma-separated `sensors` config
  key (e.g. `bme280,scd4x`).
- Round-robin refresh: one sensor read per poll cycle, taking turns; round
  duration = N × interval.
- Consolidate all readings into the same flat RuntimeStore keys (`temp_c`,
  `temp_rh`, `temp_hpa`, `temp_co2`) — no namespaced keys, no per-sensor storage.
- Display each measurement type only once (no duplication when multiple sensors
  report the same measurement).
- Aggregation strategy for overlapping measurements: **last-writer-wins** (v1);
  documented as a limitation. Per-measurement `*_source` (`avg`/index) is a
  documented v2 follow-up, NOT implemented.
- **Hard break**: remove all legacy `temp_*` config keys entirely (no aliasing,
  no deprecation window).
- Temperature precision fixed at 1 fractional digit (`%.1f`); remove the
  `temp_decimals` config key entirely.
- Unknown sensor types in the `sensors` list → log a warning and skip (do not
  silently stub).
- Deduplicate identical sensor types in the list; cap at 4 sensors (extras
  logged + ignored).
- Keep it simple; reuse existing task/watchdog infrastructure (single
  `sensor_task`, not one task per sensor).

## Decisions
- Round-robin single-task consolidation over (a) one task per sensor and (b) an
  aggregate wrapper Sensor class — simpler, saves RAM, reuses existing
  watchdog/heartbeat machinery.
- Flat consolidated RuntimeStore keys over per-sensor namespaced keys (`room_c`
  etc.) so all downstream consumers (MQTT/HA discovery, `/metrics`, `/status`,
  `{key}` placeholders) need zero changes.
- `sensors` key (not `sensors_1`/`sensors_2` numbered slots) — user rejected the
  numbered-slot idea.
- Config keys renamed to neutral names: `sensors`, `sensor_interval`,
  `sensor_readout_interval`, `sensor_display`, `sensor_hold_s`.
- Calibration offsets are per-measurement and shared across sensors:
  `temp_offset`, `humidity_offset`, `pressure_offset`, `co2_offset` (the latter
  three are new).
- `temp_decimals` dropped; temp always `%.1f` (LED `\xF7C` / runtime
  `\xC2\xB0C`).
- Display item list shows a measurement if ANY sensor in the list provides it
  (capability union), not per-sensor.
- v1 has no aggregation/smoothing; raw last-writer-wins, documented as a
  limitation.
- Back-compat is a hard break: legacy keys removed, existing users must rename
  config or fall back to `stub`.

## Config surface (new)

| Key | Default | Notes |
|-----|---------|-------|
| `sensors` | `stub` | Comma-separated driver list, max 4, deduped |
| `sensor_interval` | `300` | Per-sensor poll interval (s); round = N×interval |
| `sensor_display` | `temp,pressure` | Ordered scroll list; capability-union filtered |
| `sensor_readout_interval` | `0` | Seconds between scrolls (0 = every poll wake) |
| `sensor_hold_s` | `0` | Seconds each reading is held on screen (0 = ~2 s flash) |
| `temp_offset` | `0` | °C calibration (pre-existing, now shared) |
| `humidity_offset` | `0` | %RH calibration (new) |
| `pressure_offset` | `0` | hPa calibration (new; trend uses un-offset value) |
| `co2_offset` | `0` | ppm calibration (new) |

Removed (hard break): `temp_sensor`, `temp_interval`, `temp_display`,
`temp_decimals`, `temp_show_interval`, `temp_hold_s`.

## Work State
### Completed
- `include/sensor_types.hpp` (NEW): pure `parseSensorTypes()` + `kMaxSensors`
  (no Arduino/driver includes → host-testable).
- `include/sensor_factory.hpp` (rewritten): `createSensorList()` /
  `createSensorForType()`; parses `sensors`, dedups, caps at 4, warns+skips
  unknown; reads `bmp280_addr`/`bme280_addr`/`sht31_addr`/`ds18b20_pin` from
  DataStore.
- `src/sensor_task.cpp` (rewritten): round-robin over `std::vector<Sensor*>`
  (task owns it, deletes on exit), new config keys, offsets, flat-key writes,
  capability-union display filter, temp always `%.1f`, pressure trend fed from
  un-offset reading.
- `include/temp_display.hpp`: dropped `decimals` param from
  `ledTempFormat`/`runtimeTempFormat`/`formatTempLine` (always `%.1f`).
- `include/sensor_task.h`: doc comment updated (task now takes a vector).
- `src/main.cpp`: builds `new std::vector<Sensor*>(createSensorList())` and
  passes it to `sensor_task`.
- `src/actions_handler.cpp`: action `tempdisplay`→`sensor`, decimals control
  removed, inputs renamed to `sensor_*`.
- Sensor driver headers: doc comments `temp_sensor=X` → `sensors=X`.
- `tests/host/sensor_factory_check.cpp` (NEW): parser tests; wired into
  `run.sh`.
- `tests/host/temp_display_check.cpp`: decimals cases updated to fixed `%.1f`.
- `tests/host/prototype/glue/host_main.cpp`: builds sensor list like the device.
- `tests/host/prototype/fs/config.txt`: new key names.
- `data/config.txt`: sensor section rewritten to new keys (kept the file's
  garbled em-dash header style).
- `config.example.txt`: sensor section rewritten to new keys.
- `README.md`: features table, config table, "Sensors" section (rewritten),
  "add a new sensor" line, repo tree, MQTT example.
- `docs/SESSION_NOTES.md`: task table row + new 2026-10 "Recent changes" entry
  describing the hard break.

### Remaining (next moves)
1. **Run host tests**: `./tests/host/run.sh` — confirm the new
   `sensor_factory_check` passes and `temp_display_check` etc. still pass.
2. **Build firmware**: `~/.platformio/penv/bin/pio run -e esp32dev` (warm ~15 s;
   Flash ~67.5% / RAM ~17.9% expected). Pre-existing ArduinoJson `containsKey`
   deprecation warnings in `resto_menu_task.cpp` are expected — ignore.
3. **Fix any compile errors** surfaced by the build (e.g. the `Sensor::name()`
   call in `sensor_task.cpp` — verify the `Sensor` interface has `name()`; if
   not, log the type another way or add it to the interface).
4. **Optional host-prototype smoke run** (offline): `tests/host/prototype`
   build + run to confirm the task starts, logs, and the `stub`/list path works.
5. Re-grep for any lingering `temp_*` config references (docs/examples only)
   and confirm none remain in code.

## Relevant Files
- `include/sensor_types.hpp` (NEW, pure parser)
- `include/sensor_factory.hpp` (rewritten)
- `src/sensor_task.cpp` (rewritten)
- `include/temp_display.hpp` (decimals param dropped)
- `include/sensor_task.h` (doc comment)
- `src/main.cpp` (builds vector, passes to task)
- `src/actions_handler.cpp` (web card rename, decimals removed)
- `include/{bmp180,bmp280,bme280,sht31,aht,ds18b20,scd4x}_sensor.hpp` (doc comments)
- `include/sensor.hpp` (interface; verify `name()` exists)
- `tests/host/sensor_factory_check.cpp` (NEW), `tests/host/temp_display_check.cpp`,
  `tests/host/run.sh`, `tests/host/prototype/glue/host_main.cpp`,
  `tests/host/prototype/fs/config.txt`
- `data/config.txt`, `config.example.txt`, `README.md`, `docs/SESSION_NOTES.md`
- `include/mqtt_discovery.hpp` (flat topics — no change needed)
- `include/pressure_history.hpp` (still fed under round-robin)

## Important Context
- RuntimeStore flat keys that downstream code depends on (do NOT rename):
  `temp_c`, `temp_rh`, `temp_hpa`, `temp_co2`, plus `pressure_trend`/
  `pressure_rate`.
- `{temp_c}` placeholder output format changes to always `%.1f°C` (was `%.0f°C`
  when `temp_decimals=0`) — documented behavior change.
- Round-robin means a given sensor's reading can be up to N×`sensor_interval`
  stale before its next poll; acceptable for ambient sensors, noted in docs.
- Offsets apply to whichever sensor is active (no per-alias offsets in v1).
- `{key}` placeholder resolution order: RuntimeStore → DeviceStore → DataStore.
- pio is NOT on PATH: `~/.platformio/penv/bin/pio run -e esp32dev`.
- Repo is not a git repository; no commit/push workflow.