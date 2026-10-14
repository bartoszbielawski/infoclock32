#!/bin/sh
# Host-side tests for the pure headers in include/ (no hardware needed).
# Run from anywhere: ./tests/host/run.sh
set -e
cd "$(dirname "$0")"
CXXFLAGS="-std=c++17 -Wall -Wextra -Istubs -I../../include"

c++ $CXXFLAGS custom_message_check.cpp -o custom_message_check
c++ $CXXFLAGS sun_times_check.cpp     -o sun_times_check
c++ $CXXFLAGS life_step_check.cpp     -o life_step_check
c++ $CXXFLAGS weather_icons_check.cpp -o weather_icons_check
c++ $CXXFLAGS pressure_trend_check.cpp -o pressure_trend_check
c++ $CXXFLAGS sensor_display_check.cpp    -o sensor_display_check
c++ $CXXFLAGS sensor_factory_check.cpp  -o sensor_factory_check
c++ $CXXFLAGS sensor_merge_check.cpp    -o sensor_merge_check
c++ $CXXFLAGS sim_sensor_check.cpp      -o sim_sensor_check
c++ $CXXFLAGS secret_keys_check.cpp     -o secret_keys_check
c++ $CXXFLAGS config_utils_check.cpp    -o config_utils_check
c++ $CXXFLAGS security_utils_check.cpp -o security_utils_check
c++ $CXXFLAGS message_utils_check.cpp   -o message_utils_check
c++ $CXXFLAGS clock_face_check.cpp      -o clock_face_check
c++ $CXXFLAGS wdt_check.cpp            -o wdt_check
c++ $CXXFLAGS uptime_check.cpp         -o uptime_check
c++ $CXXFLAGS mqtt_discovery_check.cpp -o mqtt_discovery_check

# Guard: the tracked data/config.txt is written back by the device, so make
# sure no secret ever gets committed in it.
for key in wifi_password ota_password web_password ow_api_key mqtt_password; do
    val=$(grep -E "^${key}=" ../../data/config.txt | head -n1 | cut -d= -f2-)
    if [ -n "$val" ]; then
        echo "FAIL: data/config.txt has a non-empty $key - do not commit secrets"
        exit 1
    fi
done
echo "config secrets guard passed"

./custom_message_check
./sun_times_check
./life_step_check
./weather_icons_check
./pressure_trend_check
./sensor_display_check
./sensor_factory_check
./sensor_merge_check
./sim_sensor_check
./secret_keys_check
./config_utils_check
./security_utils_check
./message_utils_check
./clock_face_check
./wdt_check
./uptime_check
./mqtt_discovery_check

# ── ResourceManager handshake stress (host prototype shim) ───────────────────
echo "── resource manager stress ──"
if ( cd prototype && ./build.sh rm_stress > /dev/null 2>&1 && ./rm_stress > rm_stress.log 2>&1 ); then
    grep -E "\[rm_stress\]|all resource manager stress|forcing handover" prototype/rm_stress.log
else
    echo "resource manager stress FAILED — last log lines:"
    tail -30 prototype/rm_stress.log 2>/dev/null
    exit 1
fi
rm -f prototype/rm_stress.log
