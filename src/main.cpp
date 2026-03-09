#include <Arduino.h>
#include <WiFiManager.h>

#include <pins.hpp>

#include <create_tasks.h>
#include <hardware_init.h>
#include <resource_manager.hpp>

#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <algorithm>

#include <data_store.hpp>
#include <logger.hpp>
#include <timezone_utils.hpp>
#include <version.hpp>
#include <temp_sensor.hpp>
#include <temp_sensor_task.h>
#include <custom_message_task.h>
#include <night_mode_task.h>
#include <resto_menu_task.h>

// External task entry points
void open_weather_map_task(void *parameter);
void lhc_status_task(void *parameter);
void mqtt_task(void *parameter);
void web_server_task(void *parameter);

// Global configuration/data singleton
DataStore& dataStore = DataStore::getInstance();

// Main clock display task.
// It periodically takes display ownership, shows time, day, and date, then releases ownership.
void displayClock(void *parameter)
{
  auto& rmd = ResourceManager<LMDS>::getInstance();
  auto& matrix = rmd.getResourceRef();

  while (true)
  {
    // Try to lock display resource
    if (not rmd.make_access_request())
    {
      Serial.println("ClockDisplay: Failed to get access to display");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    // Show HH:MM:SS for 3 seconds (updated once per second)
    for (int i = 0; i < 3; i++)
    {
      matrix.clear();

      time_t now = time(nullptr);
      struct tm *timeinfo = localtime(&now);

      if (i == 0)
        logPrintf("DISP", "clock %02d:%02d:%02d",
                  timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

      // Center fixed-width time string
      int16_t x1, y1;
      uint16_t width, height;
      matrix.getTextBounds("00:00:00", 0, 0, &x1, &y1, &width, &height);
      matrix.setCursor((matrix.getSegments() * 8 - width) / 2, 0);
      matrix.printf("%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      matrix.displayToSerial(Serial);

      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    // Show day name and then date
    {
      time_t now = time(nullptr);
      struct tm *timeinfo = localtime(&now);

      static const char* const kDayNames[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
      };
      const char* dayName = kDayNames[timeinfo->tm_wday];

      char dateStr[12];
      snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
               timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);

      logPrintf("DISP", "date %s %s", dayName, dateStr);

      // Show day name centered for 2 seconds
      matrix.clear();
      int16_t x1, y1;
      uint16_t width, height;
      matrix.getTextBounds(dayName, 0, 0, &x1, &y1, &width, &height);
      matrix.setCursor((matrix.getSegments() * 8 - width) / 2, 0);
      matrix.print(dayName);
      matrix.displayToSerial(Serial);
      vTaskDelay(2000 / portTICK_PERIOD_MS);

      // Show date centered for 2 seconds
      matrix.clear();
      matrix.getTextBounds(dateStr, 0, 0, &x1, &y1, &width, &height);
      matrix.setCursor((matrix.getSegments() * 8 - width) / 2, 0);
      matrix.print(dateStr);
      matrix.displayToSerial(Serial);
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    // Release display so other tasks can draw
    rmd.release_access();

    // Idle before trying to acquire display again
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

// Utility: list files in a LittleFS directory over serial output
void listFiles(const char* dirname) {
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  File root = LittleFS.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    Serial.print("FILE: ");
    Serial.print(file.name());
    Serial.print("\tSIZE: ");
    Serial.println(file.size());
    file = root.openNextFile();
  }

  root.close();
  LittleFS.end();
  Serial.println("End of file list");
}

void setup() {
  Serial.begin(1000000);

  // Load persisted config first (hostname/timezone/task toggles, etc.)
  // Must happen before WiFi auto-connect logic in hardware_init().
  dataStore.load_from_file("/config.txt");

  // Initialize board/network and core task infrastructure
  hardware_init();
  create_tasks();

  // Configure SNTP time sources (UTC base; timezone handled separately)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Create and register LED matrix resource
  ResourceManager<LMDS>::getInstance().initialize(new LMDS(8, MATRIX_CS_PIN));

  // Logging and boot banner
  logger_init();
  logPrintf("SYS", "firmware v" APP_VERSION " built " BUILD_DATE " " BUILD_TIME);

  // Apply timezone loaded from config
  apply_timezone();

  // Restore display brightness from config, clamp to valid [0..15]
  int brightness = dataStore.get_value<int>("brightness", 7);
  brightness = max(0, min(15, brightness));
  ResourceManager<LMDS>::getInstance().getResourceRef().setIntensity((uint8_t)brightness);

  // Always-on local display task
  xTaskCreate(displayClock, "ClockTask", 4096, nullptr, 1, nullptr);

  // Optional tasks controlled via config flags
  if (dataStore.get_value<int>("enable_weather", 1))
    xTaskCreate(open_weather_map_task, "WeatherTask", 8192, nullptr, 1, nullptr);
  else
    logPrintf("SYS", "WeatherTask disabled (enable_weather=0)");

  if (dataStore.get_value<int>("enable_lhc", 1))
    xTaskCreate(lhc_status_task, "LHCStatusTask", 8192, nullptr, 1, nullptr);
  else
    logPrintf("SYS", "LHCStatusTask disabled (enable_lhc=0)");

  if (dataStore.get_value<int>("enable_mqtt", 1))
    xTaskCreate(mqtt_task, "MQTTTask", 8192, nullptr, 1, nullptr);
  else
    logPrintf("SYS", "MQTTTask disabled (enable_mqtt=0)");

  // HTTP server task
  xTaskCreate(web_server_task, "WebServerTask", 8192, nullptr, 1, nullptr);

  // Sensor/message/night mode tasks
  TempSensor* tempSensor = new StubTempSensor(); // Replace with real sensor implementation
  xTaskCreate(temp_sensor_task, "TempSensorTask", 4096, tempSensor, 1, nullptr);
  xTaskCreate(custom_message_task, "CustomMessageTask", 4096, nullptr, 1, nullptr);
  xTaskCreate(night_mode_task, "NightModeTask", 2048, nullptr, 1, nullptr);

  // Optional restaurant menu task
  if (dataStore.get_value<int>("enable_resto", 1))
    xTaskCreate(resto_menu_task, "RestoMenuTask", 8192, nullptr, 1, nullptr);
  else
    logPrintf("SYS", "RestoMenuTask disabled (enable_resto=0)");
}

// Arduino main loop is unused; FreeRTOS tasks do the work.
void loop()
{
}
