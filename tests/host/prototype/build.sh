#!/bin/sh
# Build the host prototype. Info: see README.md in this directory.
set -e
cd "$(dirname "$0")"

ROOT="${INFOCLOCK_ROOT:-$(cd ../../.. && pwd)}"

CXXFLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -g -pthread \
  -DARDUINO=10819 -DUSE_ADAFRUIT_GFX -I shim -I shim/freertos \
  -I vendor \
  -I vendor/AdafruitGFX \
  -I vendor/PubSubClient \
  -I vendor/AJSP \
  -I vendor/ArduinoJson \
  -I $ROOT/include"

FW_TASKS="clock_task weather_forecast_task lhc_status_task mqtt_task \
  temp_sensor_task custom_message_task night_mode_task sun_times_task \
  game_of_life_task resto_menu_task watchdog_task"

FW_UTIL="logger graphic_utils http_utils screen_wipe_task"

SRC="shim/arduino_shim.cpp shim/freertos_shim.cpp shim/littlefs_shim.cpp \
     shim/network_shim.cpp shim/esp_shim.cpp \
     glue/display.cpp glue/canned_responses.cpp glue/host_main.cpp"

for t in $FW_TASKS; do SRC="$SRC $ROOT/src/$t.cpp"; done
for u in $FW_UTIL; do SRC="$SRC $ROOT/src/$u.cpp"; done

SRC="$SRC vendor/LEDMatrixDriver.cpp \
         vendor/AdafruitGFX/Adafruit_GFX.cpp \
         vendor/AdafruitGFX/glcdfont.c \
         vendor/PubSubClient/PubSubClient.cpp \
         vendor/AJSP/AJSP.cpp \
         vendor/AJSP/MapCollector.cpp \
         vendor/AJSP/PathConstructor.cpp"

c++ $CXXFLAGS $SRC -o prototype
echo "built ./prototype"
