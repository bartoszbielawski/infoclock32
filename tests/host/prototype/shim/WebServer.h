#pragma once
// Host shim: minimal WebServer — the ESP32 Arduino core API subset actually
// used by the firmware web code (web_ui, http_server_task, *_handler.cpp).
// Serves over BSD sockets, one request at a time, so the real
// web_server_task + handlers run unchanged on the host.
#include "WString.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum HTTPMethod
{
    HTTP_GET = 0,
    HTTP_POST = 1
};

// OTA upload plumbing exists on the API surface only — the glue stubs for
// /update never drive an upload on the host (there is no flash to write to).
struct HTTPUpload
{
    String filename;
    String name;
    String type;
    int    status      = 0;
    size_t currentSize = 0;
    size_t totalSize   = 0;
};

#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)

using THandlerFunction = std::function<void()>;
using TUploadFunction  = std::function<void()>;

class WebServer
{
public:
    explicit WebServer(int port);
    ~WebServer();

    void on(const char* uri, THandlerFunction fn);
    void on(const char* uri, HTTPMethod method, THandlerFunction fn);
    void on(const char* uri, HTTPMethod method, THandlerFunction fn, TUploadFunction uploadFn);
    void begin();
    void handleClient();

    HTTPMethod method() const { return method_; }
    bool   hasArg(const char* name) const;
    String arg(const char* name) const;
    // Device parity: the real WebServer also takes String arguments, which
    // matters because String + int builds keys like "n0" without ambiguity.
    bool   hasArg(const String& name) const { return hasArg(name.c_str()); }
    String arg(const String& name) const { return arg(name.c_str()); }
    bool   hasHeader(const char* name) const;
    String header(const char* name) const;

    void sendHeader(const String& name, const String& value, bool first = false);
    // Responses are fully buffered, so the advertised length is irrelevant.
    void setContentLength(size_t) {}

    void send(int code, const char* content_type, const String& payload);
    void send(int code, const char* content_type, const char* payload);
    void send(int code);
    void send_P(int code, const char* content_type, const char* payload);

    void sendContent(const String& payload);
    void sendContent(const char* payload);
    void sendContent(const char* payload, size_t len);
    void sendContent_P(const char* payload);

    HTTPUpload& upload() { return upload_; }

private:
    struct Route
    {
        std::string      uri;
        int              method;    // -1 = any
        THandlerFunction fn;
        TUploadFunction  uploadFn;
    };

    void resetRequestState();
    void parseUrlencoded(const std::string& data);
    void writeResponse(int fd);

    int  port_;
    int  listen_fd_ = -1;
    std::vector<Route> routes_;

    // Per-request state.
    HTTPMethod method_ = HTTP_GET;
    std::map<std::string, std::string>    args_;
    std::vector<std::pair<std::string, std::string>> req_headers_;
    int         status_ = 0;
    std::string content_type_ = "text/html";
    std::string body_;
    std::vector<std::pair<std::string, std::string>> resp_headers_;
    HTTPUpload  upload_;
};
