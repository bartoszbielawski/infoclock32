#pragma once
// Host shim: TCP client over BSD sockets (real TCP → PubSubClient works
// against a real localhost broker) and an HTTP client used by http_utils.
#include <stdint.h>

#include "Client.h"

class WiFiClient : public Client
{
public:
    WiFiClient();
    ~WiFiClient() override;

    int connect(const char* host, uint16_t port) override;
    int connect(IPAddress ip, uint16_t port) override;
    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    void stop() override;
    uint8_t connected() override;
    operator bool() override;

protected:
    int fd_ = -1;
    uint8_t rxBuf_[1024];
    size_t rxLen_ = 0, rxPos_ = 0;
    void fillRx();
};

class WiFiClientSecure : public WiFiClient
{
public:
    void setInsecure() {}
    void setCACert(const char*) {}
};

// ── HTTP transport hook ──────────────────────────────────────────────────────
// host_http_fetch(url, body) performs a real request (via curl, so https works
// without a TLS stack). The prototype glue can install host_http_canned
// instead for --offline mode. Returns HTTP status code (>0) or negative error.

int host_http_fetch(const char* url, String& outBody);
int host_http_canned(const char* url, String& outBody);
void host_set_http_transport(int (*transport)(const char*, String&));

class HTTPClient
{
public:
    bool begin(Client&, const String& url) { url_ = url; return true; }
    void addHeader(const String&, const String&) {}
    int GET();
    String getString() const { return body_; }
    void end() { body_.clear(); url_.clear(); }

private:
    String url_;
    String body_;
};
