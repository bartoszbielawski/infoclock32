#!/bin/sh
# Host-side tests for custom_message.hpp, sun_times.hpp and life.hpp.
# No hardware needed. Run from anywhere: ./tests/host/run.sh
set -e
cd "$(dirname "$0")"
CXXFLAGS="-std=c++17 -Wall -Wextra -Istubs -I../../include"

c++ $CXXFLAGS custom_message_check.cpp -o custom_message_check
c++ $CXXFLAGS sun_times_check.cpp     -o sun_times_check
c++ $CXXFLAGS life_step_check.cpp     -o life_step_check
c++ $CXXFLAGS weather_icons_check.cpp -o weather_icons_check
c++ $CXXFLAGS pressure_trend_check.cpp -o pressure_trend_check

./custom_message_check
./sun_times_check
./life_step_check
./weather_icons_check
./pressure_trend_check
