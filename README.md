# infoclock32

An ESP32 LED matrix info display — scrolling clock, weather, messages, LHC status, and custom content.

## Hardware

- **Microcontroller**: ESP32 (tested on `esp32dev`, `esp32-c3-devkitm-1`, `esp32-s2-saola-1`, `esp32-s3-devkitc-1`)
- **Display**: 8× MAX7219 8×8 LED matrix modules (64×8 pixels), cascaded via SPI
  - Data pin: GPIO 23 (MOSI)
  - Clock pin: GPIO 18 (SCK)
  - Chip Select: GPIO 5 (configurable per target in `include/pins.hpp`)
- **WiFi**: Built-in; auto-config via captive portal on first boot
- **Storage**: LittleFS (`/config.txt` for persistent settings)
- **Logging**: Serial UART (1 Mbaud), optional UDP syslog, in-memory deque

## Features

| Feature | Status | Config |
|---------|--------|--------|
| **Clock + Date** | ✅ Live | — |
| **MQTT integration** | ✅ Live | `mqtt_server`, `mqtt_client_id`, `mqtt_user`, `mqtt_password` |
| **LHC beam status** (CERN) | ✅ Live | — |
| **Weather forecast** (OpenWeatherMap) | ✅ Live | `ow_api_key`, `ow_city_id` |
| **Custom scrolling messages** | ✅ Live | Web UI → `/actions` |
| **Temperature sensor** | ✅ Stub | Implement `TempSensor` interface, swap in `main.cpp` |
| **Night mode** (blank display at scheduled hours) | ✅ Live | `night_mode_start`, `night_mode_end` |
| **Brightness control** (0–15) | ✅ Live | Web UI or config |
| **Timezone + DST** | ✅ Live | Web UI → `/actions` (POSIX TZ strings) |
| **Hostname configuration** | ✅ Live | Web UI → `/actions` |
| **Firmware version** | ✅ Live | Displayed on home page & all pages (footer) |
| **Full activity log** | ✅ Live | Web UI → `/log` (40-entry deque + serial + syslog) |
| **HTTP OTA firmware update** | ✅ Live | Web UI → `/update` |

## Building & Flashing

**Requirements:**
- PlatformIO CLI
- Python 3.7+

**Build for your target:**
```bash
pio run -e esp32dev
```

**Upload (PlatformIO will auto-detect COM port):**
```bash
pio run -e esp32dev -t upload
```

If auto-detection fails, specify the port:
```bash
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0  # Linux
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-*  # macOS
pio run -e esp32dev -t upload --upload-port COM3  # Windows
```

**Supported targets:**
- `esp32dev` (generic ESP32)
- `esp32-c3-devkitm-1`
- `esp32-s2-saola-1`
- `esp32-s3-devkitc-1`

Adjust the environment name as needed (e.g., `esp32-c3-devkitm-1` for the C3 board).

## Configuration

### First Boot
On first boot, the device enters WiFi Manager captive portal mode:
1. Scan for WiFi network named `infoclock32` (or your configured hostname)
2. Connect from any device
3. A captive portal should auto-open; if not, manually visit `192.168.4.1`
4. Enter your home WiFi SSID and password
5. Device will reboot and connect to your network

### Web UI
Access the device at `http://<device-ip>/` or `http://<hostname>/`

**Public pages** (no auth):
- `/` — Dashboard with status, actions, and quick controls
- `/status` — Full system info (IP, hostname, uptime, RAM, firmware version)

**Protected pages** (HTTP Basic Auth: `admin` / `password`):
- `/log` — Live activity log (40 entries, auto-refresh 5s)
- `/edit` — Config editor (`/config.txt`)
- `/actions` — Advanced controls: timezone, hostname, night mode, messages
- `/update` — HTTP firmware upload
- `/reboot` — Reboot device

### Config File Format
`/config.txt` is a simple key=value file stored in LittleFS. Examples:

```
hostname=infoclock
brightness=10
timezone=UTC0
mqtt_server=192.168.x.x
mqtt_client_id=infoclock32
ow_api_key=your_openweathermap_api_key
ow_city_id=your_city_id
temp_interval=30
msg_interval=60
```

**Edit via Web UI** → `/edit` (protected), or set programmatically via **MQTT `/config` topic**.

Common keys:
- `hostname` — device name (RFC-952: letters, digits, hyphens; max 63 chars)
- `brightness` — 0–15
- `timezone` — POSIX TZ string (e.g., `Europe/Zurich`, `EST5EDT,M3.2.0,M11.1.0`)
- `mqtt_server`, `mqtt_client_id`, `mqtt_user`, `mqtt_password` — MQTT broker credentials
- `ow_api_key`, `ow_city_id` — OpenWeatherMap API key and city ID
- `temp_interval`, `msg_interval` — polling intervals in seconds

## Features in Detail

### Custom Messages

Push scrolling messages via:
1. **Web UI** (`/`) — "Push message" card
2. **MQTT** — publish to `{clientId}/push` (scroll once) or `{clientId}/looped` (repeat)
3. **Scheduled custom messages** — define start/end dates, countdown timers, and placeholders in web UI

**Message placeholders:**
- `{}` — countdown day count (e.g., "Launch in {} days")
  - Requires `message_<name>_countdown` date in config (YYYY-MM-DD format)
  - Supports "today!", "past date prefix +Nd"
- `{key}` — insert any DataStore value (e.g., `{hostname}`, `{temp_c}`, `{location}`)
  - RuntimeStore checked first (live sensor readings), then DataStore (config)

**Example custom message:**
```
Config:
  message_event_text=LS3 starts in {} days from {location}
  message_event_countdown=2026-04-20
  message_event_start=2026-03-15
  message_event_end=2026-04-21
  location=Geneva
```

### RuntimeStore (Volatile Data)

Live sensor readings and computed values that survive until reboot (not persisted to flash):
- Published by sensor tasks: `temp_c`, `temp_hpa`, `temp_rh`
- Accessible in custom messages as `{temp_c}` placeholders
- Visible on `/status` page under "⚡ Runtime values"

### Timezone & DST Support

Web UI → `/actions` → "Timezone"
- **Preset dropdown:** 20+ common timezones (US, EU, Asia, etc.)
- **Custom input:** Full POSIX TZ string (e.g., `EST5EDT,M3.2.0,M11.1.0`)
- Automatically applies DST rules via newlib's `setenv`/`tzset`

### Logging

All events log to three destinations:
1. **Serial** (1 Mbaud, UART)
2. **UDP syslog** (port 514, if `syslog_server` config key is set)
3. **In-memory deque** (40 newest entries, visible on `/log` page)

**Log tags in use:**
- `SYS` — system startup, version
- `DISP` — display activity (clock, date, scrolling messages)
- `TMP` — temperature sensor readings
- `LHC` — LHC beam status
- `WTH` — weather forecast
- `MSG` — custom message events
- `MQT` — MQTT connect/publish
- `HTTP` — HTTP client requests
- `WEB` — HTTP server events

### MQTT Integration

**Connection:**
- Broker: `mqtt_server` config
- Client ID: `mqtt_client_id` (default: `infoclock32`)
- Auth: `mqtt_user` / `mqtt_password` (optional)

**Topics:**
- `{clientId}/push <message>` — scroll once immediately
- `{clientId}/looped <message>` — store and repeat; send `{clientId}/clear` to clear
- `{clientId}/brightness <0-15>` — set display intensity
- `{clientId}/power <on|off>` — blank/unblank display
- `{clientId}/config <key=value>` — set config (blocks sensitive keys: `password`, `secret`)
- `{clientId}/reboot` — restart device
- `{clientId}/request <IP|HEAP|UPTIME|SSID|<config_key>>` → `{clientId}/publish/<name>`
- `{clientId}/status` — device publishes heartbeat JSON every 60s

**Example (mosquitto_pub):**
```bash
mosquitto_pub -h <broker_ip> -t infoclock32/push -m "Hello World!"
mosquitto_pub -h <broker_ip> -t infoclock32/brightness -m "15"
mosquitto_pub -h <broker_ip> -t infoclock32/looped -m "Status: OK"
mosquitto_pub -h <broker_ip> -t infoclock32/clear -m ""
```

Replace `<broker_ip>` with your MQTT broker's IP or hostname.

## Development

### Project Structure
```
infoclock32/
├── include/
│   ├── version.hpp              # APP_VERSION, BUILD_DATE, BUILD_TIME
│   ├── runtime_store.hpp        # Volatile KV store (FreeRTOS mutex-protected)
│   ├── timezone_utils.hpp       # apply_timezone() helper
│   ├── temp_sensor.hpp          # Abstract sensor interface + stub
│   ├── logger.hpp               # logPrintf() / logger_init()
│   ├── resource_manager.hpp     # Display access queue (Meyers singleton)
│   ├── data_store.hpp           # Config storage (LittleFS)
│   ├── pins.hpp                 # Board-specific pin definitions
│   ├── graphic_utils.hpp        # scrollMessage(), display effects
│   ├── http_utils.hpp           # HttpUtils::httpGet()
│   └── ...
├── src/
│   ├── main.cpp                 # setup() / loop() / displayClock()
│   ├── http_server_task.cpp     # Web UI (all routes)
│   ├── mqtt_task.cpp            # MQTT client
│   ├── lhc_status_task.cpp      # LHC beam status polling
│   ├── weather_forecast_task.cpp # OpenWeatherMap API
│   ├── temp_sensor_task.cpp     # Sensor reading & RuntimeStore publishing
│   ├── custom_message_task.cpp  # Scheduled message display + placeholder expansion
│   ├── night_mode_task.cpp      # Auto-blank display on schedule
│   ├── logger.cpp               # Logging system (serial + syslog + deque)
│   ├── graphic_utils.cpp        # Display rendering
│   └── ...
├── platformio.ini               # Build config (4 target boards)
└── README.md                    # You are here
```

### Adding a New Feature

**Example: integrate a real temperature sensor**

1. **Implement the `TempSensor` interface** in a new header file (e.g., `include/bme680_sensor.hpp`):
   ```cpp
   #include <temp_sensor.hpp>

   class BME680Sensor : public TempSensor {
   public:
       bool begin() override { /* init I2C */ }
       bool read() override { /* poll sensor */ }
       float temperature() const override { return temp_c_; }
       bool hasPressure() const override { return true; }
       float pressure() const override { return pressure_hpa_; }
       // ... humidity if available

   private:
       float temp_c_, pressure_hpa_;
   };
   ```

2. **Swap in `main.cpp`:**
   ```cpp
   #include <bme680_sensor.hpp>

   // In setup():
   TempSensor* tempSensor = new BME680Sensor();
   xTaskCreate(temp_sensor_task, "TempSensorTask", 4096, tempSensor, 1, nullptr);
   ```

3. **The sensor will automatically:**
   - Publish `temp_c`, `temp_hpa`, etc. to RuntimeStore every 30 seconds (configurable)
   - Make values available in custom messages as `{temp_c}` / `{temp_hpa}`
   - Appear in `/status` → "⚡ Runtime values" section
   - Log each reading to `/log` with tag `TMP`

### C++ Standard Notes

The esp32dev toolchain (GCC 8.4.0) defaults to **C++14 mode**. Avoid C++17-only features:
- ❌ Use `std::enable_if<>::type` instead of `enable_if_t`
- ❌ Use `std::is_same<>::value` instead of `is_same_v`

The ESP32-C3 custom framework uses C++17, so these work there but break on esp32dev.

### Branches

- `main` — stable, tested firmware
- `experimental` — today's WIP, frequent rebuilds

## Troubleshooting

**Device not appearing on network?**
- Ensure it has power and is within WiFi range
- Check captive portal (scan for `infoclock32` or configured hostname AP)
- Monitor Serial output at 1 Mbaud for boot messages

**Web UI not loading?**
- Verify IP address on status page or router
- Try clearing browser cache
- Check `/log` for HTTP errors

**Messages not scrolling?**
- Verify display is not in night mode (check `/status`)
- Check `/log` for `DISP` entries (confirms message was processed)
- Ensure brightness > 0 (set via `/actions` card on home page)

**MQTT not connecting?**
- Verify broker is reachable and credentials are correct (`/edit` config)
- Check `/log` for `MQT` tag entries
- Monitor broker logs for auth failures

## License

MIT (or your preferred license)

## Author

Made with ☕ for ESP32 projects everywhere.
