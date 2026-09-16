// Host shim implementation: ESP system functions.
#include "esp_system.h"

#include <chrono>
#include <cstdlib>
#include <random>
#include <thread>

static std::mt19937 rng{std::random_device{}()};

void esp_restart()
{
    // give the terminal renderer a moment to show the last frame
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    fprintf(stdout, "\n[prototype] esp_restart() — process exits (rerun to continue)\n");
    std::exit(0);
}

esp_reset_reason_t esp_reset_reason() { return ESP_RST_SW; }

uint32_t esp_random() { return rng(); }

uint32_t esp_get_free_heap_size() { return 180000; }
uint32_t esp_get_minimum_free_heap_size() { return 120000; }
