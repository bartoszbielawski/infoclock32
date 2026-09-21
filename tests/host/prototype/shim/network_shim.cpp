// Host shim implementation: WiFi global, BSD-socket client, UDP, HTTP via curl.
#include "WiFi.h"
#include "WiFiUdp.h"
#include "WiFiClient.h"
#include "esp_system.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>

WiFiClass WiFi;
EspClass ESP;

// ── WiFiClient ───────────────────────────────────────────────────────────────

WiFiClient::WiFiClient() = default;
WiFiClient::~WiFiClient() { stop(); }

int WiFiClient::connect(const char* host, uint16_t port)
{
    stop();

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(host, portStr, &hints, &result) != 0 || !result) return 0;

    fd_ = -1;
    for (addrinfo* ai = result; ai; ai = ai->ai_next)
    {
        fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd_ < 0) continue;
        if (::connect(fd_, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(result);

    if (fd_ < 0) return 0;

    timeval tv{};
    tv.tv_sec = 10;   // bounded socket reads: a dead peer never hangs the task forever
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return 1;
}

int WiFiClient::connect(IPAddress ip, uint16_t port)
{
    return connect(ip.toString().c_str(), port);
}

void WiFiClient::fillRx()
{
    if (rxPos_ < rxLen_) return;
    rxLen_ = rxPos_ = 0;
    if (fd_ < 0) return;
    ssize_t n = recv(fd_, rxBuf_, sizeof(rxBuf_), MSG_DONTWAIT);
    if (n > 0)
    {
        rxLen_ = (size_t)n;
        rxPos_ = 0;
    }
}

int WiFiClient::available()
{
    fillRx();
    return (int)(rxLen_ - rxPos_);
}

int WiFiClient::read()
{
    fillRx();
    if (rxPos_ >= rxLen_) return -1;
    return rxBuf_[rxPos_++];
}

int WiFiClient::peek()
{
    fillRx();
    if (rxPos_ >= rxLen_) return -1;
    return rxBuf_[rxPos_];
}

size_t WiFiClient::write(uint8_t c)
{
    return write(&c, 1);
}

size_t WiFiClient::write(const uint8_t* buffer, size_t size)
{
    if (fd_ < 0) return 0;
    ssize_t n = send(fd_, buffer, size, 0);
    return n > 0 ? (size_t)n : 0;
}

void WiFiClient::stop()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
    rxLen_ = rxPos_ = 0;
}

uint8_t WiFiClient::connected()
{
    return (fd_ >= 0) ? 1 : 0;
}

WiFiClient::operator bool()
{
    return connected();
}

// ── WiFiUDP ──────────────────────────────────────────────────────────────────

WiFiUDP::~WiFiUDP() { stop(); }

int WiFiUDP::begin(uint16_t)
{
    if (fd_ < 0) fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    return fd_ >= 0 ? 1 : -1;
}

bool WiFiUDP::beginPacket(const char* host, uint16_t port)
{
    stop();
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo* result = nullptr;
    if (getaddrinfo(host, portStr, &hints, &result) != 0 || !result)
    {
        stop();
        return false;
    }
    int rc = ::connect(fd_, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    if (rc != 0)
    {
        stop();
        return false;
    }
    packet_.clear();
    return true;
}

bool WiFiUDP::endPacket()
{
    if (fd_ < 0) return false;
    ssize_t n = send(fd_, packet_.data(), packet_.size(), 0);
    return n >= 0;
}

void WiFiUDP::stop()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

// ── HTTP via curl ────────────────────────────────────────────────────────────

static int (*httpTransport)(const char*, String&) = nullptr;
void host_set_http_transport(int (*transport)(const char*, String&)) { httpTransport = transport; }

int host_http_fetch(const char* url, String& outBody)
{
    outBody = String();

    // Single-quoted for the shell; URLs in this project never contain quotes.
    std::string cmd = "curl -sS -m 25 -w '\\n%{http_code}' '";
    cmd += url;
    cmd += "' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;

    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) all.append(buf, n);
    int rc = pclose(pipe);

    if (rc != 0) return -1;

    // last line = status code
    auto lastNl = all.rfind('\n');
    if (lastNl == std::string::npos) return -1;
    int code = atoi(all.c_str() + lastNl + 1);
    if (code <= 0) return -1;
    outBody = String(all.substr(0, lastNl));
    return code;
}

int HTTPClient::GET()
{
    if (httpTransport) return httpTransport(url_.c_str(), body_);
    return host_http_fetch(url_.c_str(), body_);
}
