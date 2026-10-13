#pragma once
// Host shim: Arduino.h entry header. Include-path order makes this shadow the
// real ESP32 Arduino core when building the prototype.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "WString.h"
#include "Printable.h"
#include "Print.h"
#include "Stream.h"
#include "pgmspace.h"

#include <string>
#include <algorithm>
#include <cmath>

// min/max like the ESP32 core
using std::min;
using std::max;

// ── timing ───────────────────────────────────────────────────────────────────
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

// Arduino random()
long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);

// ── GPIO (captured by the display decoder, never touches hardware) ───────────
#define OUTPUT  1
#define INPUT   0
#define HIGH    1
#define LOW     0

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int  digitalRead(int pin);

// Callback invoked on every digitalWrite(pin, value). Used by the display
// decoder to detect CS edges.
using HostGpioHook = void (*)(int pin, int value);
void host_set_gpio_hook(HostGpioHook hook);

// ── serial ───────────────────────────────────────────────────────────────────
class SerialClass : public Print
{
public:
    void begin(unsigned long) {}
    operator bool() const { return true; }
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
};
extern SerialClass Serial;

// ── misc constants ───────────────────────────────────────────────────────────
#define PI 3.1415926535897932384626433832795
#define DEG_TO_RAD 0.017453292519943295
#define RAD_TO_DEG 57.295779513082320876798154814105
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)

typedef bool boolean;

// On the real ESP32 Arduino core, `byte` is a global typedef (uint8_t) and is
// the only `byte` in scope. On the host (libc++, C++17), <cstddef>/<cstdint>
// leak `std::byte` into unqualified name lookup, which makes the firmware's
// unqualified `byte` (e.g. `byte *payload` in mqtt_task.cpp) ambiguous.
// We can't rename the firmware usages (the prototype never modifies firmware
// sources), so we back the shim's `byte` with a *macro* expanding to uint8_t.
// Macro substitution happens before name lookup, so unqualified `byte` becomes
// `uint8_t` and can never collide with std::byte. (In the firmware, `byte` is
// only ever used as a type, never as an identifier, so the macro is safe.)
#define byte uint8_t

int strlcpy_host(char* dst, const char* src, size_t size);
#if defined(__APPLE__)
#define HAVE_STRLCPY 1   // BSD libc provides strlcpy
#endif
#ifndef HAVE_STRLCPY
#define strlcpy(dst, src, size) strlcpy_host((dst), (src), (size))
#endif

// The real Arduino core exposes the FreeRTOS API transitively; firmware
// headers (resource_manager.hpp, runtime_store.hpp, …) rely on that.
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include "WiFiClient.h"

// cooperative yield: no-op on the host
inline void yield() {}
inline void esp_yield() {}
