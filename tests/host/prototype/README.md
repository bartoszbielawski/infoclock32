# infoclock32 — host prototype

Runs the real firmware task code on macOS/Linux with no hardware: 11 tasks as
`std::thread`s, the LED matrix rendered in the terminal, config from a plain
directory. Useful for logic demos, the watchdog recovery cycle, and display
arbitration behaviour — not for hardware timing.

## Build & run

```bash
./build.sh
./prototype --offline --static   # deterministic canned API responses
./prototype                      # online: real HTTP (curl) + real MQTT to localhost:1883
```

Flags:

| Flag | Effect |
|------|--------|
| `--offline` | canned responses for weather/LHC/resto; MQTT task skipped |
| `--static` | plain text frames instead of ANSI live view |
| `--hang <Task>` | stop that task's heartbeats 20 s after boot (watchdog demo) |
| `--fs <dir>` | filesystem root for `/config.txt` (default `./fs`) |

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
- **Display**: the SPI shim forwards every 16-bit transfer to a MAX7219 frame
  decoder; CS edges (GPIO shim) latch rows into a framebuffer that renders to
  the terminal (~4 fps, exactly the LEDMatrixDriver pixel layout).
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
