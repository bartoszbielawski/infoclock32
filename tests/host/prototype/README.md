# infoclock32 — host prototype

Runs the real firmware task code on macOS/Linux with no hardware: 11 tasks as
`std::thread`s, the LED matrix rendered in the terminal, config from a plain
directory. Useful for logic demos, the watchdog recovery cycle, and display
arbitration behaviour — not for hardware timing.

## Build & run

```bash
./build.sh [prototype|rm_stress]   # default: both
./prototype --offline --static     # deterministic canned API responses
./prototype                        # online: real HTTP (curl) + real MQTT to localhost:1883
```

`rm_stress` is a standalone stress test for the `ResourceManager` display
handshake (no display, no fs): an abandoned-request regression phase (a
timed-out requester whose request is still queued must not stall the manager)
plus randomized contention (6 threads, mixed blocking/timed/priority acquires)
asserting mutual exclusion and termination. `tests/host/run.sh` builds and
runs it; per-event traces go to stderr.

## Web UI on the host

The prototype serves the real web UI (real `web_server_task`, handlers, and
auth) via a minimal `WebServer` shim over BSD sockets:

- Port **8080** (binding 80 needs root on the host); override with
  `INFOCLOCK_HTTP_PORT`. The URL is printed at startup.
- Auth uses `web_password` from the config root (`--fs`) — HTTP Basic, same
  as the device. `/push` stays unauthenticated by design.
- `/update` is a host stub (there is no flash to write to); everything else
  (`/`, `/status`, `/actions`, `/messages`, `/edit`, `/wifi`, `/log`,
  `/api/*`, `/push`) is real firmware code.
- Requests serialize through the single WebServerTask exactly like on the
  device: a push that scrolls for 20 s delays later requests — display access
  failures surface as HTTP 503 / "Display busy" instead of hangs.

Quick checks:

```bash
curl "localhost:8080/push?msg=hello"            # scrolls on the display
curl -o /dev/null -w '%{http_code}\n' localhost:8080/api/status   # 401/200
curl -u admin:secret localhost:8080/api/status  # 200 when web_password=secret
```

Flags:

| Flag | Effect |
|------|--------|
| `--offline` | canned responses for weather/LHC/resto; MQTT task skipped |
| `--static` | plain text frames instead of ANSI live view |
| `--hang <Task>` | stop that task's heartbeats 20 s after boot (watchdog demo) |
| `--fs <dir>` | filesystem root for `/config.txt` (default `./fs`) |

Output streams: display frames go to **stdout**, log lines to **stderr** —
e.g. `./prototype 2>/dev/null` shows only the display.

Set `INFOCLOCK_ROOT` to build against a firmware tree outside the repo.

## The watchdog demo

```bash
./prototype --offline --static --hang ClockTask
...
[WDT] Clock stale — warning (reboot on next stale check)
[WDT] Clock stale twice — rebooting
[prototype] esp_restart() — process exits (rerun to continue)
```

Same warn-once → reboot pipeline as on device, exercised by the real
supervisor and registry code.

## How it works

```
shim/    generic Arduino/FreeRTOS fakes — project-agnostic, reusable
glue/    project-specific: host_main, MAX7219 decoder/renderer, canned data
vendor/  copied libraries (LEDMatrixDriver, Adafruit GFX, PubSubClient, AJSP,
         ArduinoJson) — resync from .pio/libdeps/<env>/ when versions change
fs/      runtime config (like LittleFS /config.txt)
```

- **Tasks**: `xTaskCreate` → `std::thread`, `vTaskDelay` → sleep, queues/
  semaphores/notifications → mutex + condvar. `--hang` works by parking the
  named task inside `vTaskDelay` after 20 s of uptime.
- **Display**: the renderer polls the LMDS framebuffer directly (`getPixel`,
  the same ground truth as the driver's `displayToSerial`) at ~4 fps from a
  background thread. No SPI decoding — wire-order assumptions removed.
- **Storage**: LittleFS maps onto `fs/` — the real `DataStore` code loads and
  saves `/config.txt` unchanged.
- **Network**: HTTPClient shim shells out to `curl` (so https works); MQTT
  uses the real PubSubClient over BSD sockets — install a local broker
  (`mosquitto`) and `mosquitto_pub -t infoclock32-proto/push -m hi` scrolls.
- **Time**: the host clock replaces NTP; the timezone config still applies via
  `TZ`/`tzset`.

## What is *not* modelled

Single-core scheduling fairness and priorities, real SPI/I2C timing, TWDT
panic semantics (the supervisor's host "reboot" exits the process), RTC
memory across reboots, brownout, flash corruption, WiFi behaviour.

## Firmware separation

The prototype never modifies firmware sources: no `#ifdef HOST` anywhere in
`src/` or `include/`; everything is absorbed by `shim/` headers via include
path order. The compiled firmware file list lives in `build.sh` — when a task
is added or removed in the firmware, update that one list. Deleting this
directory leaves the firmware tree untouched.
