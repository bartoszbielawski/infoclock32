#pragma once

// Central declarations of all task entry points. Tasks with a dedicated
// header are re-exported so any code that starts tasks (main.cpp on device,
// host prototype glue) needs exactly one include.

#include <temp_sensor_task.h>
#include <custom_message_task.h>
#include <night_mode_task.h>
#include <resto_menu_task.h>
#include <ota_task.h>

// Tasks defined without a dedicated header:
void displayClock(void *parameter);
const char* localizedDayName(int wday);
void open_weather_map_task(void *parameter);
void lhc_status_task(void *parameter);
void mqtt_task(void *parameter);
void web_server_task(void *parameter);
void sun_times_task(void *parameter);
void game_of_life_task(void *parameter);
