#include <Arduino.h>
#include <ctime>

#include <data_store.hpp>
#include <logger.hpp>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>

// Day-name table for the configured language (en/fr/pl), see "language" config key
const char* localizedDayName(int wday)
{
  static const char* const kDayNamesEn[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char* const kDayNamesFr[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
  static const char* const kDayNamesPl[] = {"Ndz","Pon","Wto","Sro","Czw","Pia","Sob"};

  std::string lang = DataStore::getInstance().get_value("language", "en");
  const char* const* dayNames = kDayNamesEn;
  if (lang == "fr") dayNames = kDayNamesFr;
  else if (lang == "pl") dayNames = kDayNamesPl;
  return dayNames[wday];
}

// Main clock display task.
// It periodically takes display ownership, shows time, day, and date, then releases ownership.
void displayClock(void *parameter)
{
  registerTask("Clock", 4096, 60000);
  auto& rmd = ResourceManager<LMDS>::getInstance();

  while (true)
  {
    task_heartbeat();
    if (auto display = rmd.acquire())
    {
      // Show HH:MM:SS for 3 seconds (updated once per second)
      for (int i = 0; i < 5; i++)
      {
        display->clear();

        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);

        if (i == 0)
          logPrintf("DISP", "clock %02d:%02d:%02d",
                    timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

        int16_t x1, y1;
        uint16_t width, height;
        display->getTextBounds("00:00:00", 0, 0, &x1, &y1, &width, &height);
        display->setCursor((display->getSegments() * 8 - width) / 2, 0);
        display->printf("%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        display->display();

        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }

      // Show day name and date for 2 seconds
      {
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);

        const char* dayName = localizedDayName(timeinfo->tm_wday);

        char dateStr[16];
        snprintf(dateStr, sizeof(dateStr), "%s %02d/%02d",
                 dayName, timeinfo->tm_mday, timeinfo->tm_mon + 1);

        logPrintf("DISP", "date %s", dateStr);

        int16_t x1, y1;
        uint16_t width, height;
        display->clear();
        display->getTextBounds(dateStr, 0, 0, &x1, &y1, &width, &height);
        display->setCursor((display->getSegments() * 8 - width) / 2, 0);
        display->print(dateStr);
        display->display();
        vTaskDelay(2000 / portTICK_PERIOD_MS);
      }
    } // display released here

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
