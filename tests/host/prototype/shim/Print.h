#pragma once
// Host shim: Arduino Print base class (subset used by the firmware).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "WString.h"
#include "Printable.h"

class Print
{
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size)
    {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }
    virtual void flush() {}

    size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
    size_t print(const char s[]) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { return print((long)v); }
    size_t print(unsigned v) { return print((unsigned long)v); }
    size_t print(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); return print(b); }
    size_t print(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); return print(b); }
    size_t print(double v, int digits = 2)
    { char b[40]; snprintf(b, sizeof(b), "%.*f", digits, v); return print(b); }

    size_t println() { return write((uint8_t)'\n'); }
    template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }

    int printf(const char* format, ...)
    {
        char buf[512];
        va_list args;
        va_start(args, format);
        int n = vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (n > 0) print(buf);
        return n;
    }
};
