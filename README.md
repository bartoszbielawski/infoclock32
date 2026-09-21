# infoclock32

[![CI](https://github.com/bartoszbielawski/infoclock32/actions/workflows/ci.yml/badge.svg)](https://github.com/bartoszbielawski/infoclock32/actions/workflows/ci.yml)

An ESP32 LED matrix info display — scrolling clock, weather, LHC beam status, temperature, custom messages, and more.

## Hardware

- **Microcontroller**: ESP32 family (see supported targets below)
- **Display**: 8× MAX7219 8×8 LED matrix modules (64×8 pixels), cascaded via SPI
- **WiFi**: Built-in; auto-config via captive portal on first boot
- **Storage**: LittleFS (`/config.txt` for persistent settings)
- **Logging**: Serial UART (1 Mbaud), optional UDP syslog, in-memory ring buffer

### Pin assignments

Pins are board-specific and defined in `include/pins.hpp`.

| Board | SDA | SCL | SPI CS | SPI SCK | SPI MOSI |
|-------|-----|-----|--------|---------|----------|
| ESP32-C3 | GPIO 1 | GPIO 0 | GPIO 7 | GPIO 4 | GPIO 6 |
| ESP32-S2 | GPIO 8 | GPIO 9 | GPIO 5 | GPIO 36 | GPIO 35 |
| ESP32-S3 | GPIO 8 | GPIO 9 | GPIO 5 | GPIO 12 | GPIO 11 |
| ESP32 (original) | GPIO 21 | GPIO 22 | GPIO 5 | GPIO 18 | GPIO 23 |

## Features

| Feature | Config key(s) |
|---------|--------------|
| Clock + date display | `language` (`en`/`fr`/`pl`), `timezone` |
| Weather forecast (OpenWeatherMap) | `enable_weather`, `ow_api_key`, `ow_city_id` (condition icon prepended to the scroll; `weather_id`/`weather_desc` placeholders) |
| LHC beam status (CERN) | `enable_lhc` |
| Temperature sensor (multiple drivers) | `temp_sensor`, `temp_interval`, `temp_offset` |
| Custom scrolling messages | `message_<name>_*`, `msg_interval` |
| Night mode (auto-dim) | `night_start`, `night_end`, `night_brightness` |
| MQTT integration | `enable_mqtt`, `mqtt_server`, `mqtt_client_id`, … |
| Restaurant menu (CERN Novae) | `enable_resto`, `novae_codes`, `resto_restaurants`, `resto_start_hour`, `resto_end_hour` |
| Sunrise/sunset widget (offline math) | `enable_sun`, `sun_lat`, `sun_lon`, `sun_interval_min` |
| Game of Life idle animation | `enable_life`, `life_interval_s`, `life_burst_s`, `life_min_hold_s`, `life_seed_display`, `life_fuel_pct` |
| Wipe transition between display hand-offs | `wipe_interval` (0 = off) |
| OTA firmware update (wireless) | `ota_password` (disabled if unset) |
| HTTP firmware upload | Web UI → `/update` |
| Web configuration editor | Web UI → `/edit` |

## Building & Flashing

**Requirements:** PlatformIO CLI, Python 3.7+

```bash
# Build (defaults to esp32-c3-devkitm-1 if platformio.local.ini sets it)
pio run

# Build for a specific target
pio run -e esp32dev

# Flash firmware
pio run -e esp32dev -t upload

# Upload filesystem (config + web assets)
pio run -e esp32dev -t uploadfs

# Serial monitor
pio device monitor -b 1000000
```

**Supported targets:** `esp32dev`, `esp32-c3-devkitm-1`, `esp32-s2-saola-1`, `esp32-s3-devkitc-1`

### Releases & CI

GitHub Actions builds all four targets on every push to `main` and on PRs; the `firmware-<env>` binaries are attached as artifacts to each run, and host-side tests run in CI as well. Pushing a `v*` tag (e.g. `git tag v1.0 && git push origin v1.0`) additionally publishes a [GitHub Release](https://github.com/bartoszbielawski/infoclock32/releases) with named, flashable `.bin` files for every target.

### OTA flashing

Set `ota_password` in config, then create `platformio.local.ini`:

```ini
[env:esp32-c3-devkitm-1]
upload_protocol = espota
upload_port = infoclock32.local
```

The first flash must always be wired.

### Remote reflashing

A device already on the network can be updated without a cable, using a `.bin` for the right target from [Releases](https://github.com/bartoszbielawski/infoclock32/releases) (e.g. `infoclock32-v0.1.0-esp32-c3-devkitm-1.bin` for an ESP32-C3):

- **Web upload** (any build with the `/update` page) — browse to `http://<device>/update` and upload the file, or script it:

  ```bash
  curl -u ":<web_password>" \
       -F "firmware=@infoclock32-v0.1.0-esp32-c3-devkitm-1.bin" \
       http://infoclock32.local/update
  ```

  Omit `-u` when `web_password` is empty. The device reboots itself on success.

- **ArduinoOTA** (if `ota_password` is set) — `pio run -e esp32-c3-devkitm-1 -t upload` with the `platformio.local.ini` from the OTA section above.

If the running firmware has neither the `/update` page nor `ota_password` configured, there is no remote path — flash it once wired, after which both methods are available (they are part of the current firmware). `uploadfs` is not possible remotely; the filesystem keeps its existing `/config.txt` across OTA updates.

## Configuration

### First boot

On a freshly flashed device LittleFS is empty — all settings use defaults until the user saves via `/edit` or MQTT `/config`.

Connection is handled by [WiFiManager](https://github.com/tzapu/WiFiManager). If `wifi_ssid` is not configured (or connecting fails), the device opens a captive-portal AP named `<hostname>-setup` (default: `infoclock32-setup`) at `192.168.4.1` for `wifi_portal_timeout_s` seconds (default 180), then boots offline — a background monitor keeps retrying and reconnects whenever the network returns. Reboot the device to reopen the setup portal. You can also change credentials without the portal via the `/wifi` web page (writes `wifi_ssid`/`wifi_password` to config, then reboot).

### Web UI

Access at `http://<device-ip>/` or `http://<hostname>.local/`

| Route | Auth | Description |
|-------|------|-------------|
| `GET /` | — | Dashboard: status + quick controls (push, brightness, reboot) |
| `GET /status` | — | Full system info (IP, heap, uptime, tasks), auto-refresh |
| `GET /log` | ✓ | Live log viewer (40 entries, newest first, auto-refresh 5 s) |
| `GET /log/entries` | ✓ | Log JSON feed (`?since=<seq>` delta polling) |
| `GET /edit` · `POST /edit` | ✓ | Edit `/config.txt`; reloads DataStore on save |
| `GET /actions` · `POST /actions` | ✓ | Push message, brightness, night mode, timezone, hostname, web password, display reset, reboot |
| `GET /messages` · `POST /messages` | ✓ | Custom message slot editor (text, date window, countdown) |
| `GET /wifi` · `POST /wifi` | ✓ | Network scan + credentials; writes `wifi_ssid`/`wifi_password`, then suggests reboot |
| `GET /update` · `POST /update` | ✓ | HTTP firmware upload (.bin) |
| `POST /reboot` | ✓ | Restart device |
| `GET /api/status` | ✓ | JSON status (uptime, heap, RSSI, MQTT, display diagnostics) |
| `GET /api/runtime` | ✓ | JSON RuntimeStore snapshot |
| `GET /push` · `POST /push` | — | JSON message push: `?msg=Hello&speed=<ms>` (10–500); 503 "display busy" when the display can't be acquired |

Auth: HTTP Basic with any username and the `web_password` config value. Leave `web_password` empty to disable auth entirely. Note that `/push` is intentionally unauthenticated so scripts and automations can post messages without credentials.

### Config file

`/config.txt` is a `key=value` file on LittleFS. Lines starting with `#` are comments. A fully-commented example is included in `data/config.txt` and uploaded with `uploadfs`.

Key config keys:

| Key | Default | Description |
|-----|---------|-------------|
| `wifi_ssid` / `wifi_password` | — | WiFi credentials |
| `wifi_portal_timeout_s` | `180` | seconds the setup portal stays open at boot (30–600) |
| `hostname` | `infoclock32` | DHCP name, mDNS `.local`, AP portal name |
| `brightness` | `7` | Display intensity 0–15 |
| `timezone` | `UTC0` | POSIX TZ string (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`) |
| `language` | `en` | Date labels: `en`, `fr`, `pl` |
| `night_start` / `night_end` | — | Night mode window (HH:MM); blank to disable |
| `night_brightness` | `1` | Intensity during night hours |
| `web_password` | — | HTTP Basic Auth password (blank = open) |
| `ota_password` | — | ArduinoOTA password; blank disables OTA entirely |
| `syslog_server` | — | UDP syslog destination IP (port 514) |
| `ow_api_key` / `ow_city_id` | — | OpenWeatherMap credentials |
| `mqtt_server` / `mqtt_client_id` | — | MQTT broker and client ID |
| `mqtt_user` / `mqtt_password` | — | MQTT credentials |
| `temp_sensor` | `stub` | Sensor driver (see below) |
| `temp_interval` | `300` | Sensor poll interval in seconds (the shipped `data/config.txt` pins `30`) |
| `temp_offset` | `0` | Temperature correction in °C added to readings |
| `msg_interval` | `60` | Custom message cycle interval (seconds) |
| `sun_lat` / `sun_lon` | — | WGS84 position for the sunrise/sunset widget (blank = task not started) |
| `sun_interval_min` | `30` | How often the sun line scrolls (minutes, 5–720) |
| `display_min_hold_s` | `10` | Seconds a long hold (scroll, Life burst) keeps the display before the clock may preempt it (0 = yield at once) |
| `life_interval_s` / `life_burst_s` | `300` / `30` | Game of Life: pause between bursts (30–3600) / burst length (5–120, seconds) |
| `life_min_hold_s` | `display_min_hold_s` | Game of Life: per-burst override of the minimum display slice |
| `life_seed_display` | `1` | Game of Life: seed each new board from whatever is on the matrix (clock, last message); `0` = random soup |
| `life_fuel_pct` | `18` | Game of Life: random cells added around an image seed so thin strokes survive (0-50) |
| `wipe_interval` | `10` | Minutes between decorative wipe transitions on display hand-off; `0` disables |
| `spi_sck` / `spi_mosi` / `spi_cs` | board defaults | Override the matrix SPI pins from `pins.hpp` |
| `i2c_sda` / `i2c_scl` | board defaults | Override the I2C pins from `pins.hpp` |
| `resto_restaurants` | `3` | Comma-separated Novae restaurant numbers |
| `resto_start_hour` / `resto_end_hour` | `9` / `14` | Hours between which the menu is fetched and displayed (end exclusive; window wraps midnight) |
| `resto_test_date` | — | `YYYY-MM-DD` override for menu fetches; also bypasses the display window (testing) |
| `enable_weather` / `enable_lhc` / `enable_mqtt` / `enable_resto` / `enable_sun` | `1` | Enable/disable individual tasks |
| `enable_life` | `0` | Game of Life idle animation (off by default) |

## Temperature sensor

Set `temp_sensor` in config to one of the supported drivers:

| Value | Sensor | Interface | Extra keys |
|-------|--------|-----------|------------|
| `bmp180` | BMP180 | I2C 0x77 (fixed) | — |
| `bmp280` | BMP280 | I2C | `bmp280_addr` (default `0x76`) |
| `bme280` | BME280 | I2C | `bme280_addr` (default `0x76`) |
| `sht31` | SHT31 | I2C | `sht31_addr` (default `0x44`) |
| `aht10` / `aht20` | AHT10 / AHT20 | I2C 0x38 (fixed) | — |
| `ds18b20` | DS18B20 | 1-Wire | `ds18b20_pin` (default `4`) |
| `stub` (default) | None | — | — |

Sensor readings are published to RuntimeStore and visible on the `/status` page: `temp_c` (string including `°C`, e.g. `22.5°C`), `temp_hpa` (integer hPa), `temp_rh` (%). Use them in custom messages as `{temp_c}` placeholders.

Sensors with pressure also get a **barometric trend**: the pressure readout on the display is prefixed with a trend arrow glyph (single/double up or down arrows, or → for steady), and RuntimeStore publishes `pressure_trend` (`rising fast`/`rising`/`steady`/`falling`/`falling fast`) and `pressure_rate` (hPa/hour). The trend is a least-squares slope over recent readings — it needs about 30 minutes of samples after boot before the arrow appears.

To add a new sensor, implement the `TempSensor` interface in a new header and add an entry to `createTempSensor()` in `include/temp_sensor_factory.hpp`.

## Custom messages

Define named message slots in config:

```ini
# Always-on message
message_hello_text=Hello world!

# Date-windowed message
message_lunch_text=Lunch time!
message_lunch_start=2026-09-01
message_lunch_end=2026-09-30

# Countdown — shows "Conference in: 45d", "today!" on the day, then hides
message_event_text=Conference in
message_event_countdown=2026-09-01
```

Messages cycle every `msg_interval` seconds. Day counts are calendar days (local midnight to midnight). A countdown auto-hides from the day after its target unless `message_<name>_end` keeps it visible for count-ups (rendered as `+Nd`).

Placeholders are expanded at display time: `{}` inserts the countdown day count, and `{key}` inserts the first hit from RuntimeStore (sensor values, `weather_id`, `weather_desc`, `wdt_culprit`, …), then DeviceStore (`ip`, `hostname`, `ssid`, `heap`, `uptime`, `mac`, `rssi`, `version`, …), then DataStore config keys. Unknown keys are left as-is.

Push a one-off message via the `/` dashboard, `/push`, MQTT, or `/actions`.

## Restaurant menu (CERN Novae)

Fetches lunch (`midi`) menus from api.mynovae.ch for the configured restaurants and scrolls them between `resto_start_hour` and `resto_end_hour` (the window wraps midnight). Restaurant numbers map to Novae salepoints: `1` = R1, `2` = R2, `3` = R3. Dishes are deduplicated per restaurant, shown in the configured `language` (`fr` falls back to `en`; there is no Polish menu), and each menu is separated by a pause so other tasks can display in between. Menus refresh hourly. `novae_codes` identifies your CERN group to the API (default `CER103`).

## Display messages on boot and reboot

- **Boot**: scrolls firmware version and reset reason (e.g. `v1.2.3 | rst: power on`, or `v1.2.3 | rst: wdt (MQTT)` after a watchdog restart)
- **Setup portal**: when booting into the captive portal, scrolls a hint to join `<hostname>-setup` and browse `192.168.4.1`
- **Date check**: after NTP sync, scrolls the current day, date, and time so the timezone config can be verified at a glance (skipped if time hasn't synced within 10 s)
- **Reboot**: scrolls `Rebooting` before the CPU restarts, regardless of trigger (web UI, MQTT, OTA)

## Display scheduling

Content tasks compete for the display through the `ResourceManager` request queue; the clock and user-initiated push messages (web `/push`, `/actions`, MQTT) queue on a **priority lane** and are always served ahead of queued sensor/status updates, so the clock can never be starved. Long holds (scrolls, Life bursts) keep the display for at least `display_min_hold_s` and then yield as soon as a priority request is waiting.

Two safety nets guard against wedged holders: web and MQTT interactive paths acquire the display with short timeouts and report "display busy" (HTTP 503) instead of blocking, and MQTT pushes that arrive while the display is busy are re-queued and retried. A holder that stops making progress for 30 s has the display force-handed over (counted and shown on `/status`).

Between hand-offs the display optionally plays a random wipe transition (`wipe_interval`, at most once per interval; skipped when a priority request is waiting).

## Watchdog

An always-on watchdog supervisor restarts the device if a task stops making progress. Each task heartbeats the supervisor once per loop iteration (registered with a per-task timeout in `TaskRegistry`); the supervisor itself is watched by the hardware task watchdog. Enforcement is **warn-once**: the first stale check for a task logs a `WDT` warning, the second consecutive one reboots the device. Tasks announce planned long waits (poll intervals, failure cooldowns) via grace extensions so normal operation never trips it, and the Game of Life burst broadcasts grace while it holds the display.

After a watchdog reboot the boot banner shows the culprit (e.g. `v1.2.3 | rst: wdt (MQTT)`), the event is logged (`SYS` tag), and `wdt_culprit` is published to RuntimeStore — queryable via `/status` and MQTT `…/request`.

## MQTT integration

Client ID defaults to `mqtt_client_id` config key (default: `infoclock32`). All topics are prefixed with the client ID.

| Topic | Payload | Action |
|-------|---------|--------|
| `…/push` | text | Scroll once immediately |
| `…/looped` | text | Store and repeat; shown whenever the display is free |
| `…/clear` | — | Clear the looped message |
| `…/brightness` | `0`–`15` | Set display intensity (persisted to config) |
| `…/config` | `key=value` | Set DataStore key (keys containing `password`/`secret` are blocked) |
| `…/reboot` | — | Restart device |
| `…/request` | key name | Reply to `…/publish/<name>`; looks up DeviceStore (system values like `ip`, `heap`, `uptime`, `ssid`, `rssi`, `mac`, `version`, …), then RuntimeStore (sensor readings, `weather_*`, `wdt_culprit`), then DataStore config keys. Keys are case-sensitive; any key containing `assword` is refused |
| `…/status` | — | Device publishes heartbeat JSON (`ip`, `heap`, `uptime`, `ssid`) here every 60 s |

```bash
mosquitto_pub -h <broker> -t infoclock32/push -m "Hello!"
mosquitto_pub -h <broker> -t infoclock32/brightness -m "12"
mosquitto_pub -h <broker> -t infoclock32/config -m "temp_interval=60"
```

## Logging

All events are written to three destinations simultaneously:

1. **Serial** — 1 Mbaud UART
2. **UDP syslog** — port 514 (set `syslog_server` to enable)
3. **In-memory ring buffer** — last 40 entries, visible at `/log`

Log tags: `SYS`, `DISP`, `TMP`, `WTH`, `LHC`, `MSG`, `NGT`, `SUN`, `LIFE`, `RST` (resto), `SCR`, `MQT`, `HTTP`, `WEB`, `OTA`, `WIFI`, `RES`, `WDT`

## Development

### Project structure

```
infoclock32/
├── include/
│   ├── pins.hpp                  # Board-specific pin definitions
│   ├── temp_sensor.hpp           # TempSensor abstract base + StubTempSensor
│   ├── temp_sensor_factory.hpp   # createTempSensor() — picks driver from config
│   ├── *_sensor.hpp              # Concrete sensor implementations
│   ├── resource_manager.hpp      # Display arbiter (priority lane, RAII guard, force handover)
│   ├── task_registry.hpp         # Self-registering task list (watchdog + /status)
│   ├── runtime_store.hpp         # Volatile KV store (sensor readings, etc.)
│   ├── device_store.hpp          # Read-only device parameters (ip, heap, uptime, …)
│   ├── data_store.hpp            # Persistent config (LittleFS key=value)
│   ├── custom_message.hpp        # Message slots, date windows, countdowns, placeholders
│   ├── life.hpp / sun_times.hpp  # Game of Life step + solar math (pure, host-testable)
│   ├── weather_icons.hpp         # Condition + trend arrow bitmaps
│   ├── pressure_trend.hpp        # Barometric trend classification + slope
│   ├── reboot_utils.hpp          # reboot_with_message() — show "Rebooting" then restart
│   ├── logger.hpp                # logPrintf() / logger_init()
│   ├── graphic_utils.hpp         # scrollMessage(), wipe effects
│   └── http_utils.hpp            # HttpUtils::httpGet()
├── src/
│   ├── main.cpp                  # setup(): init, task creation, boot banner
│   ├── clock_task.cpp            # Clock/date display task (priority lane)
│   ├── hardware_init.cpp         # WiFiManager provisioning + reconnect monitor
│   ├── http_server_task.cpp      # Web server task — routing + /status /log /edit
│   ├── web_ui.cpp                # Shared layout, CSS, HTTP Basic auth
│   ├── actions_handler.cpp       # /actions + /push handlers
│   ├── messages_handler.cpp      # /messages editor
│   ├── wifi_handler.cpp          # /wifi scan + credentials
│   ├── update_handler.cpp        # /update firmware upload
│   ├── mqtt_task.cpp             # MQTT client
│   ├── lhc_status_task.cpp       # LHC beam status polling
│   ├── weather_forecast_task.cpp # OpenWeatherMap forecast
│   ├── temp_sensor_task.cpp      # Sensor polling + RuntimeStore publishing
│   ├── custom_message_task.cpp   # Scheduled messages
│   ├── night_mode_task.cpp       # Auto-dim on schedule
│   ├── sun_times_task.cpp        # Sunrise/sunset widget
│   ├── game_of_life_task.cpp     # Game of Life idle bursts
│   ├── resto_menu_task.cpp       # CERN Novae lunch menus
│   ├── screen_wipe_task.cpp      # Transition effect on display hand-off
│   ├── ota_task.cpp              # ArduinoOTA (gated on ota_password)
│   ├── watchdog_task.cpp         # Watchdog supervisor
│   ├── logger.cpp                # Serial + syslog + ring buffer backend
│   └── graphic_utils.cpp         # Display rendering helpers
├── data/
│   └── config.txt                # Default config (uploaded to LittleFS with uploadfs)
├── tests/host/                   # Host-side checks + desktop prototype
└── platformio.ini                # Build config — 4 target boards
```

### C++ compatibility

The `esp32dev` toolchain (GCC 8.4.0) compiles in **C++14**. Avoid C++17-only aliases:

| Use | Not |
|-----|-----|
| `std::enable_if<…>::type` | `enable_if_t` |
| `std::is_same<…>::value` | `is_same_v` |

The ESP32-C3 custom framework build uses C++17, so these work there but break on `esp32dev`.

### Branches

- `main` — stable releases

## Troubleshooting

**Device not on the network** — check Serial output at 1 Mbaud; look for the IP address printed at boot. If the AP `infoclock32-setup` appears, WiFi credentials are missing or wrong.

**Web UI not loading** — verify IP, try `http://<hostname>.local/`; check `/log` for HTTP errors.

**Display blank** — check night mode schedule on `/status`; verify brightness > 0 in `/actions`.

**MQTT not connecting** — check `MQT` tag in `/log`; verify broker reachability and credentials in `/edit`.

**OTA not working** — `ota_password` must be non-empty; first flash is always wired.
