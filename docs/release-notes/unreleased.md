## Unreleased

### Highlights

- **Several weather locations.** Up to 4 places, shown in turn. Each is a set of keys named after a short id (lowercase letters and digits): `weather_<id>_lat` / `_lon` (Open-Meteo), `weather_<id>_city_id` (OpenWeatherMap), `weather_<id>_name` (label) and an optional `weather_<id>_provider` to mix backends. Locations are found by scanning the config, in alphabetical order of their ids. Per-location placeholders: `{weather_<id>_temp}`, `{weather_<id>_desc}`, `{weather_<id>_id}`.
- **Warnings for deprecated config.** The web home page and `/edit` show an amber box listing deprecated keys in use, and the LED scrolls "Check your config" every `config_warn_interval_min` minutes (default 60, `0` = off) while any remain.

### Deprecations (removed in 0.9.0)

| Deprecated | Use instead |
|---|---|
| `ow_api_key` | `weather_api_key` |
| `ow_city_id` | `weather_<id>_city_id` (single location: `weather_city_id`) |
| `ow_interval_min` | `weather_interval_min` (now applies to every location) |
| `{weather_temp}`, `{weather_desc}`, `{weather_id}` | `{weather_<id>_temp}` etc. They mirror the first location until removal. |

Existing configs keep working unchanged. The single-location keys `weather_lat` / `weather_lon` / `weather_name` are used only while no `weather_<id>_*` location exists; once one does they are ignored, and the web UI says so.

### Upgrading

- OTA is enough; the partition layout is unchanged.
- To move to the new names, rename `ow_api_key` to `weather_api_key`, and give each place its own `weather_<id>_*` keys (see `config.example.txt`).
