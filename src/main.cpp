#include <Arduino.h>

#include <pins.hpp>

#include <hardware_init.h>
#include <resource_manager.hpp>

#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <algorithm>

#include <data_store.hpp>
#include <logger.hpp>
#include <timezone_utils.hpp>
#include <version.hpp>
#include <Wire.h>
#include <temp_sensor.hpp>
#include <temp_sensor_factory.hpp>
#include <temp_sensor_task.h>
#include <custom_message_task.h>
#include <night_mode_task.h>
#include <resto_menu_task.h>
#include <ota_task.h>
#include <screen_wipe_task.h>
#include <task_registry.hpp>

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
  registerTask("Clock", 4096);
  auto& rmd = ResourceManager<LMDS>::getInstance();

  while (true)
  {
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

        static const char* const kDayNamesEn[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* const kDayNamesFr[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
        static const char* const kDayNamesPl[] = {"Ndz","Pon","Wto","Sro","Czw","Pia","Sob"};

        std::string lang = dataStore.get_value("language", "en");
        const char* const* dayNames = kDayNamesEn;
        if (lang == "fr") dayNames = kDayNamesFr;
        else if (lang == "pl") dayNames = kDayNamesPl;
        const char* dayName = dayNames[timeinfo->tm_wday];

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

// Utility: list files in a LittleFS directory over serial output
void listFiles(const char* dirname) {
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
  Serial.println("End of file list");
}

void setup() {
  Serial.begin(1000000);
#if ARDUINO_USB_CDC_ON_BOOT
  // HWCDC: wait up to 2 s for the host to open the port so early log lines aren't lost.
  // On standalone boot (no PC connected) this times out and continues normally.
  { unsigned long t = millis(); while (!Serial && millis() - t < 2000) delay(10); }
#endif

  LittleFS.begin(true);

  // Load persisted config first (hostname/timezone/task toggles, etc.)
  // Must happen before WiFi auto-connect logic in hardware_init().
  dataStore.load_from_file("/config.txt");

  pinMode(LED_BLINK_PIN, OUTPUT);

  // Initialize board/network and core task infrastructure
  hardware_init();

  // Configure SNTP time sources (UTC base; timezone handled separately)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Configure SPI bus with explicit pins before constructing the LED matrix driver.
  // Pin defaults come from pins.hpp; override via config keys spi_sck/spi_mosi/spi_cs.
  int spiSck  = dataStore.get_value<int>("spi_sck",  MATRIX_SCK_PIN);
  int spiMosi = dataStore.get_value<int>("spi_mosi", MATRIX_MOSI_PIN);
  int spiCs   = dataStore.get_value<int>("spi_cs",   MATRIX_CS_PIN);
  SPI.begin(spiSck, /*miso=*/-1, spiMosi);

  // Create and register LED matrix resource
  auto* lmds = new LMDS(SPI, SPISettings(5000000, MSBFIRST, SPI_MODE0), 8, spiCs);
  lmds->begin();
  ResourceManager<LMDS>::getInstance().initialize(lmds);
  ResourceManager<LMDS>::getInstance().setPreReleaseHook(wipe_on_release);

  // Logging and boot banner
  logger_init();
  logPrintf("SYS", "firmware v" APP_VERSION " built " BUILD_DATE " " BUILD_TIME);

  // Apply timezone loaded from config
  apply_timezone();

  auto& rmd = ResourceManager<LMDS>::getInstance();
  // Restore display brightness from config, clamp to valid [0..15]
  int brightness = dataStore.get_value<int>("brightness", 7);
  brightness = max(0, min(15, brightness));
  rmd.getResourceRef().setIntensity((uint8_t)brightness);

  if (auto display = rmd.acquire())
  {
    // Show firmware version + reset reason so it's visible on every boot
    static const char* const resetReasonStr[] = {
      "unknown", "power on", "external rst", "software rst",
      "panic", "interrupt wdt", "task wdt", "watchdog",
      "deepsleep rst", "brownout", "SDIO rst"
    };
    esp_reset_reason_t reason = esp_reset_reason();
    int reasonIdx = (int)reason < (int)(sizeof(resetReasonStr)/sizeof(resetReasonStr[0]))
                    ? (int)reason : 0;
    char bootMsg[64];
    snprintf(bootMsg, sizeof(bootMsg), APP_VERSION " | rst: %s", resetReasonStr[reasonIdx]);
    scrollMessage(bootMsg, display, 30);
    if (wifi_is_ap_mode()) {
      std::string apMsg = "WiFi setup: connect to "
                          + dataStore.get_value("hostname", "infoclock32")
                          + "-setup  then browse 192.168.4.1/edit";
      scrollMessage(apMsg, display, 40);
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }

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
  xTaskCreate(web_server_task, "WebServerTask", 10240, nullptr, 1, nullptr);

  // Sensor/message/night mode tasks
  // I2C pin defaults from pins.hpp; override via config keys i2c_sda/i2c_scl.
  int i2cSda = dataStore.get_value<int>("i2c_sda", I2C_SDA_PIN);
  int i2cScl = dataStore.get_value<int>("i2c_scl", I2C_SCL_PIN);
  Wire.begin(i2cSda, i2cScl);
  TempSensor* tempSensor = createTempSensor();  // sensor type from temp_sensor config key
  xTaskCreate(temp_sensor_task, "TempSensorTask", 4096, tempSensor, 1, nullptr);
  xTaskCreate(custom_message_task, "CustomMessageTask", 4096, nullptr, 1, nullptr);
  xTaskCreate(night_mode_task, "NightModeTask", 2048, nullptr, 1, nullptr);


  if (!dataStore.get_value("ota_password", "").empty())
    xTaskCreate(ota_task, "OTATask", 4096, nullptr, 2, nullptr);
  else
    logPrintf("SYS", "OTATask disabled (ota_password not set)");

  // Optional restaurant menu task
  if (dataStore.get_value<int>("enable_resto", 1))
    xTaskCreate(resto_menu_task, "RestoMenuTask", 8192, nullptr, 1, nullptr);
  else
    logPrintf("SYS", "RestoMenuTask disabled (enable_resto=0)");


  
  //by this time we should have received the correct time from NTP, so we can scroll the date to verify that timezone is applied correctly
}

// loopTask is a real FreeRTOS task so vTaskDelay yields properly here.
void loop()
{
  digitalWrite(LED_BLINK_PIN, HIGH);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  digitalWrite(LED_BLINK_PIN, LOW);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
