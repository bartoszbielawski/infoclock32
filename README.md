# infoclock32

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
| Weather forecast (OpenWeatherMap) | `enable_weather`, `ow_api_key`, `ow_city_id` |
| LHC beam status (CERN) | `enable_lhc` |
| Temperature sensor (multiple drivers) | `temp_sensor`, `temp_interval` |
| Custom scrolling messages | `message_<name>_*`, `msg_interval` |
| Night mode (auto-dim/blank) | `night_start`, `night_end`, `night_brightness` |
| MQTT integration | `enable_mqtt`, `mqtt_server`, `mqtt_client_id`, … |
| Restaurant menu (CERN Novae) | `enable_resto`, `novae_codes` |
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

### OTA flashing

Set `ota_password` in config, then create `platformio.local.ini`:

```ini
[env:esp32-c3-devkitm-1]
upload_protocol = espota
upload_port = infoclock32.local
```

The first flash must always be wired.

## Configuration

### First boot

On a freshly flashed device LittleFS is empty — all settings use defaults until the user saves via `/edit` or MQTT `/config`.

If `wifi_ssid` is not configured the device starts a WiFi setup AP named `<hostname>-setup` (default: `infoclock32-setup`). Connect to it and browse to `192.168.4.1/edit` to set credentials, then reboot.

### Web UI

Access at `http://<device-ip>/` or `http://<hostname>.local/`

| Route | Auth | Description |
|-------|------|-------------|
| `GET /` | — | Dashboard: status + quick controls |
| `GET /status` | — | Full system info (IP, heap, uptime, tasks), auto-refresh |
| `GET /log` | ✓ | Live log viewer (40 entries, newest first, auto-refresh 5 s) |
| `GET /edit` | ✓ | Edit `/config.txt`; reloads DataStore on save |
| `GET /actions` | ✓ | Push message, brightness, power on/off, reboot |
| `GET /update` | ✓ | HTTP firmware upload |
| `POST /reboot` | ✓ | Restart device |

Auth: HTTP Basic with any username and the `web_password` config value. Leave `web_password` empty to disable auth entirely.

### Config file

`/config.txt` is a `key=value` file on LittleFS. Lines starting with `#` are comments. A fully-commented example is included in `data/config.txt` and uploaded with `uploadfs`.

Key config keys:

| Key | Default | Description |
|-----|---------|-------------|
| `wifi_ssid` / `wifi_password` | — | WiFi credentials |
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
| `temp_interval` | `30` | Sensor poll interval (seconds) |
| `msg_interval` | `60` | Custom message cycle interval (seconds) |
| `enable_weather` / `enable_lhc` / `enable_mqtt` / `enable_resto` | `1` | Enable/disable individual tasks |

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

Sensor readings are published to RuntimeStore (`temp_c`, `temp_hpa`, `temp_rh`) and visible on the `/status` page. Use them in custom messages as `{temp_c}` placeholders.

To add a new sensor, implement the `TempSensor` interface in a new header and add an entry to `createTempSensor()` in `include/temp_sensor_factory.hpp`.

## Custom messages

Define named message slots in config:

```ini
# Always-on message
message_hello_text=Hello world!

# Time-windowed message
message_lunch_text=Lunch time!
message_lunch_start=12:00
message_lunch_end=13:00

# Countdown
message_event_text=Conference in
message_event_countdown=2026-09-01 09:00
```

Messages cycle every `msg_interval` seconds. Placeholders like `{temp_c}` are expanded from RuntimeStore and DataStore at display time.

Push a one-off message via the `/` dashboard, MQTT, or `/actions`.

## Display messages on boot and reboot

- **Boot**: scrolls firmware version and reset reason (e.g. `v1.2.3 | rst: power on`)
- **Reboot**: scrolls `Rebooting` before the CPU restarts, regardless of trigger (web UI, MQTT, OTA)

## MQTT integration

Client ID defaults to `mqtt_client_id` config key (default: `infoclock32`). All topics are prefixed with the client ID.

| Topic | Payload | Action |
|-------|---------|--------|
| `…/push` | text | Scroll once immediately |
| `…/looped` | text | Store and repeat; send empty to clear |
| `…/brightness` | `0`–`15` | Set display intensity |
| `…/power` | `on`\|`off` | Blank / unblank display |
| `…/config` | `key=value` | Set DataStore key (keys containing `password`/`secret` are blocked) |
| `…/reboot` | — | Restart device |
| `…/request` | `IP`\|`HEAP`\|`UPTIME`\|`SSID`\|`<key>` | Reply to `…/publish/<name>` |
| `…/status` | — | Device publishes heartbeat JSON here every 60 s |

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

Log tags: `SYS`, `DISP`, `TMP`, `LHC`, `WTH`, `MSG`, `MQT`, `HTTP`, `WEB`, `OTA`, `RES`

## Development

### Project structure

```
infoclock32/
├── include/
│   ├── pins.hpp                  # Board-specific pin definitions
│   ├── temp_sensor.hpp           # TempSensor abstract base + StubTempSensor
│   ├── temp_sensor_factory.hpp   # createTempSensor() — picks driver from config
│   ├── bmp180/280/bme280/…       # Concrete sensor implementations
│   ├── resource_manager.hpp      # RAII display access (ResourceGuard + drop counter)
│   ├── task_registry.hpp         # Self-registering FreeRTOS task list (for /status)
│   ├── runtime_store.hpp         # Volatile KV store (sensor readings, etc.)
│   ├── data_store.hpp            # Persistent config (LittleFS key=value)
│   ├── reboot_utils.hpp          # reboot_with_message() — show "Rebooting" then restart
│   ├── logger.hpp                # logPrintf() / logger_init()
│   ├── graphic_utils.hpp         # scrollMessage(), display effects
│   └── http_utils.hpp            # HttpUtils::httpGet()
├── src/
│   ├── main.cpp                  # setup() / loop() / displayClock task
│   ├── http_server_task.cpp      # Web UI — all routes
│   ├── mqtt_task.cpp             # MQTT client
│   ├── lhc_status_task.cpp       # LHC beam status polling
│   ├── weather_forecast_task.cpp # OpenWeatherMap forecast
│   ├── temp_sensor_task.cpp      # Sensor polling + RuntimeStore publishing
│   ├── custom_message_task.cpp   # Scheduled messages + placeholder expansion
│   ├── night_mode_task.cpp       # Auto-dim on schedule
│   ├── ota_task.cpp              # ArduinoOTA (gated on ota_password)
│   ├── logger.cpp                # Serial + syslog + deque backend
│   └── graphic_utils.cpp         # Display rendering helpers
├── data/
│   └── config.txt                # Default config (uploaded to LittleFS with uploadfs)
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
- `experimental` — current WIP

## Troubleshooting

**Device not on the network** — check Serial output at 1 Mbaud; look for the IP address printed at boot. If the AP `infoclock32-setup` appears, WiFi credentials are missing or wrong.

**Web UI not loading** — verify IP, try `http://<hostname>.local/`; check `/log` for HTTP errors.

**Display blank** — check night mode schedule on `/status`; verify brightness > 0 in `/actions`.

**MQTT not connecting** — check `MQT` tag in `/log`; verify broker reachability and credentials in `/edit`.

**OTA not working** — `ota_password` must be non-empty; first flash is always wired.
