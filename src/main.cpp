#include <Arduino.h>
#include <WiFiManager.h>

#include <create_tasks.h>
#include <hardware_init.h>
#include <resource_manager.hpp>

#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <algorithm>

LMDS matrix(8, 5); // 8 modules, CS pin 5

ResourceManager<decltype(matrix)> displayManager;

void animateDisplay(void *parameter)
{
  matrix.clear();
  int count = 0;
  int totalPixels = matrix.getSegments() * 8 * 8;
  while (true)
  {
    if (displayManager.make_access_request())
    {
      bool target_state = count < totalPixels / 2;
      if (target_state)
        count++;
      else
        count--;
      
      int x = random(0, matrix.getSegments() * 8);
      int y = random(0, 8);

      while (matrix.getPixel(x, y) == target_state) {
        x = random(0, matrix.getSegments() * 8);
        y = random(0, 8);
      }

      matrix.setPixel(x, y, target_state);
      
      matrix.displayToSerial(Serial);
      displayManager.release_access();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


void displayClock(void *parameter)
{
  while (true)
  {
    if (not displayManager.make_access_request())
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
    
    //wipeDisplayLeftToRight(matrix, 5);
    scollOutDisplayRight(matrix, 5);

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    displayManager.release_access();
    
    // wait a bit before updating again and requesting access again
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void marqueeDisplay(void *parameter)
{  
  while (true)
  {   
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    if (not displayManager.make_access_request())
    {
      Serial.println("MarqueeDisplay: Failed to get access to display");
      continue;
    }


    scollMessage("ESP32-C3!", matrix, 50);

    displayManager.release_access();
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void setup() {
  hardware_init();
  create_tasks();

  //NTP client
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  //initialize the resource manager with the matrix object
  displayManager.initialize(&matrix);

  //xTaskCreate(animateDisplay, "DisplayTask", 2048, nullptr, 1, nullptr);
  xTaskCreate(displayClock, "ClockTask", 2048, nullptr, 1, nullptr);
  xTaskCreate(marqueeDisplay, "MarqueeTask", 2048, nullptr, 1, nullptr);
}

void loop() 
{
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  Serial.println("Main loop running...");
}
