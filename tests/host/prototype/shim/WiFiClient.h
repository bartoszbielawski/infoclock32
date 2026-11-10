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
    void setCACertBundle(const uint8_t*) {}   // device parity; curl does TLS on the host
    void setCACert(const char*) {}
};

// ── HTTP transport hook ──────────────────────────────────────────────────────
// host_http_fetch(url, body) performs a real request (via curl, so https works
// without a TLS stack). The prototype glue can install host_http_canned
// instead for --offline mode. Returns HTTP status code (>0) or negative error.

int host_http_fetch(const char* url, String& outBody);
int host_http_canned(const char* url, String& outBody);
void host_set_http_transport(int (*transport)(const char*, String&));

// Reads a String as a Stream (the shim fetches the whole body up front).
class StringBodyStream : public Stream
{
public:
    void reset(const String* s) { s_ = s; pos_ = 0; }
    int available() override { return s_ ? (int)(s_->length() - pos_) : 0; }
    int read() override { return available() > 0 ? (unsigned char)(*s_)[pos_++] : -1; }
    int peek() override { return available() > 0 ? (unsigned char)(*s_)[pos_] : -1; }
    size_t write(uint8_t) override { return 0; }
private:
    const String* s_ = nullptr;
    size_t pos_ = 0;
};

class HTTPClient
{
public:
    bool begin(Client&, const String& url) { url_ = url; return true; }
    void addHeader(const String&, const String&) {}
    void useHTTP10(bool) {}
    int GET();
    String getString() const { return body_; }
    int getSize() const { return (int)body_.length(); }
    Stream& getStream() { stream_.reset(&body_); return stream_; }
    void end() { body_.clear(); url_.clear(); }

private:
    String url_;
    String body_;
    StringBodyStream stream_;
};
