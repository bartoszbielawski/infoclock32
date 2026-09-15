#!/bin/sh
# Host-side tests for custom_message.hpp (no hardware needed).
# Run from anywhere: ./tests/host/run.sh
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -Wall -Wextra -Istubs -I../../include custom_message_check.cpp -o custom_message_check
./custom_message_check
