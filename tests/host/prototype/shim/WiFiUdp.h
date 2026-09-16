#pragma once
// Host shim: WiFiUDP with real UDP sockets (logger syslog output works).
#include "Arduino.h"

class WiFiUDP : public Print
{
public:
    ~WiFiUDP() override;

    int begin(uint16_t port);   // bind (optional for sending)
    bool beginPacket(const char* host, uint16_t port);
    bool endPacket();
    void stop();

    size_t write(uint8_t c) override { packet_ += (char)c; return 1; }
    size_t write(const uint8_t* buffer, size_t size) override
    { packet_.append((const char*)buffer, size); return size; }

private:
    int fd_ = -1;
    std::string host_;
    uint16_t port_ = 0;
    std::string packet_;
};
