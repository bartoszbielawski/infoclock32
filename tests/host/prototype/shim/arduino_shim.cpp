// Host shim implementation: timing, GPIO capture, serial, random.
#include "Arduino.h"
#include "SPI.h"

#include <chrono>
#include <thread>
#include <mutex>
#include <cstdio>
#include <random>

unsigned long millis()
{
    using namespace std::chrono;
    return (unsigned long)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

unsigned long micros()
{
    using namespace std::chrono;
    return (unsigned long)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void delayMicroseconds(unsigned int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

static std::mt19937 rng{std::random_device{}()};
void randomSeed(unsigned long s) { rng.seed((unsigned long)s); }

long random(long max)
{
    if (max <= 0) return 0;
    return (long)(rng() % (unsigned long)max);
}

long random(long min, long max)
{
    if (max <= min) return min;
    return min + random(max - min);
}

// ── GPIO capture ─────────────────────────────────────────────────────────────
static HostGpioHook gpioHook = nullptr;
static int pinState[64] = {0};

void host_set_gpio_hook(HostGpioHook hook) { gpioHook = hook; }

void pinMode(int, int) {}
void digitalWrite(int pin, int value)
{
    if (pin >= 0 && pin < 64) pinState[pin] = value;
    if (gpioHook) gpioHook(pin, value);
}
int digitalRead(int pin) { return (pin >= 0 && pin < 64) ? pinState[pin] : 0; }

// ── serial ───────────────────────────────────────────────────────────────────
static std::mutex serialMutex;

size_t SerialClass::write(uint8_t c)
{
    std::lock_guard<std::mutex> lock(serialMutex);
    fputc(c, stderr);   // logs go to stderr; display frames own stdout
    return 1;
}

size_t SerialClass::write(const uint8_t* buffer, size_t size)
{
    std::lock_guard<std::mutex> lock(serialMutex);
    fwrite(buffer, 1, size, stderr);
    return size;
}

SerialClass Serial;

SPIClass SPI;

int strlcpy_host(char* dst, const char* src, size_t size)
{
    if (!dst || !src || size == 0) return 0;
    size_t len = strlen(src);
    size_t n = (len < size - 1) ? len : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return (int)len;
}
