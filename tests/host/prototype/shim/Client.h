#pragma once
// Host shim: minimal IP address + Client base (what PubSubClient needs).
#include <stdint.h>
#include <string>

#include "WString.h"
#include "Stream.h"

class IPAddress
{
public:
    IPAddress() { bytes_[0] = bytes_[1] = bytes_[2] = bytes_[3] = 0; }
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    { bytes_[0] = a; bytes_[1] = b; bytes_[2] = c; bytes_[3] = d; }

    String toString() const
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
        return String(buf);
    }

    bool operator==(const IPAddress& o) const
    {
        for (int i = 0; i < 4; i++) if (bytes_[i] != o.bytes_[i]) return false;
        return true;
    }

    uint32_t asUint32() const
    {
        return ((uint32_t)bytes_[0] << 24) | ((uint32_t)bytes_[1] << 16) |
               ((uint32_t)bytes_[2] << 8) | bytes_[3];
    }

private:
    uint8_t bytes_[4];
};

class Client : public Stream
{
public:
    virtual int connect(const char* host, uint16_t port) = 0;
    virtual int connect(IPAddress ip, uint16_t port) = 0;
    virtual int available() override = 0;
    virtual int read() override = 0;
    virtual int peek() override = 0;
    virtual size_t write(uint8_t) override = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) override = 0;
    virtual void stop() = 0;
    virtual uint8_t connected() = 0;
    virtual operator bool() = 0;
};
