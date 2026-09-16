#pragma once
// Host shim: ESP-IDF system bits used by the firmware.
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>

// esp_restart ends the prototype process (documented limitation: RTC_NOINIT
// state does not survive a host "reboot").
void esp_restart();

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason();
uint32_t esp_random();

uint32_t esp_get_free_heap_size();
uint32_t esp_get_minimum_free_heap_size();

class EspClass
{
public:
    const char* getChipModel() const { return "ESP32 (host prototype)"; }
    const char* getSdkVersion() const { return "host-prototype"; }
    uint32_t getCpuFreqMHz() const { return 0; }
    void restart() const { esp_restart(); }
};
extern EspClass ESP;
