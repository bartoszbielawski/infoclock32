#pragma once
// Host shim: Arduino Stream base class (subset used by the firmware).
#include "Print.h"

// Arduino.h declares millis(), but Arduino.h includes this header first.
unsigned long millis();

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;

    void setTimeout(unsigned long timeout) { timeout_ = timeout; }
    unsigned long getTimeout() const { return timeout_; }

    int timedRead()
    {
        unsigned long start = millis();
        do {
            int c = read();
            if (c >= 0) return c;
        } while (millis() - start < timeout_);
        return -1;
    }

    String readStringUntil(char terminator)
    {
        String out;
        int c = timedRead();
        while (c >= 0 && c != terminator)
        {
            out += (char)c;
            c = timedRead();
        }
        return out;
    }

    virtual size_t readBytesUntil(char terminator, char* buffer, size_t length)
    {
        size_t n = 0;
        while (n < length)
        {
            int c = timedRead();
            if (c < 0 || c == terminator) break;
            buffer[n++] = (char)c;
        }
        return n;
    }

    virtual size_t readBytes(char* buffer, size_t length)
    {
        size_t n = 0;
        while (n < length)
        {
            int c = timedRead();
            if (c < 0) break;
            buffer[n++] = (char)c;
        }
        return n;
    }

protected:
    unsigned long timeout_ = 1000;
};
