#include <Arduino.h>
#include <WiFiManager.h>

#include <create_tasks.h>
#include <hardware_init.h>
#include <resource_manager.hpp>

#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <algorithm>

#include <data_store.hpp>

void open_weather_map_task(void *parameter);
void lhc_status_task(void *parameter);

DataStore& dataStore = DataStore::getInstance();

// void animateDisplay(void *parameter)
// {
//   matrix.clear();
//   int count = 0;
//   int totalPixels = matrix.getSegments() * 8 * 8;
//   while (true)
//   {
//     if (displayManager.make_access_request())
//     {
//       bool target_state = count < totalPixels / 2;
//       if (target_state)
//         count++;
//       else
//         count--;
      
//       int x = random(0, matrix.getSegments() * 8);
//       int y = random(0, 8);

//       while (matrix.getPixel(x, y) == target_state) {
//         x = random(0, matrix.getSegments() * 8);
//         y = random(0, 8);
//       }

//       matrix.setPixel(x, y, target_state);
      
//       matrix.displayToSerial(Serial);
//       displayManager.release_access();
//     }
//     vTaskDelay(100 / portTICK_PERIOD_MS);
//   }
// }


void displayClock(void *parameter)
{
  auto& rmd = ResourceManager<LMDS>::getInstance();
  auto& matrix = rmd.getResourceRef();

  while (true)
  {
    if (not rmd.make_access_request())
    {
      Serial.println("ClockDisplay: Failed to get access to display");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    
    //print the time
    for (int i = 0; i < 3; i++)
    {
      matrix.clear();


      time_t now = time(nullptr);
      struct tm *timeinfo = localtime(&now);

      Serial.printf("Current time: %02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      
      //print to the matrix centered
      int16_t x1, y1;
      uint16_t width, height;

      
      matrix.getTextBounds("00:00:00", 0, 0, &x1, &y1, &width, &height);
      matrix.setCursor((matrix.getSegments() * 8 - width) / 2, 0);
      matrix.printf("%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      matrix.displayToSerial(Serial);

      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    matrix.clear();

    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);

    Serial.printf("Current date: %04d-%02d-%02d\n", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    
    matrix.setCursor(2, 0);
    matrix.printf("%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    matrix.displayToSerial(Serial);
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    rmd.release_access();
    
    // wait a bit before updating again and requesting access again
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void marqueeDisplay(void *parameter)
{  
  auto& rmd = ResourceManager<LMDS>::getInstance();
  auto& matrix = rmd.getResourceRef();
  while (true)
  {   
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (not rmd.make_access_request())
    {
      Serial.println("MarqueeDisplay: Failed to get access to display");
      continue;
    }


    scrollMessage("ESP32-C3!", matrix, 50);

    rmd.release_access();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

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
  hardware_init();
  create_tasks();

  //NTP client
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  ResourceManager<LMDS>::getInstance().initialize(new LMDS(8, 5)); // 8 modules, CS pin 5

  dataStore.load_from_file("/config.txt");

  //xTaskCreate(animateDisplay, "DisplayTask", 2048, nullptr, 1, nullptr);
  xTaskCreate(displayClock, "ClockTask", 2048, nullptr, 1, nullptr);
  //xTaskCreate(marqueeDisplay, "MarqueeTask", 2048, nullptr, 1, nullptr);
  //xTaskCreate(open_weather_map_task, "WeatherTask", 8192, nullptr, 1, nullptr);
  xTaskCreate(lhc_status_task, "LHCStatusTask", 8192, nullptr, 1, nullptr);

  
}

void loop() 
{
}
