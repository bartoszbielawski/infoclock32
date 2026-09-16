// Host shim implementation: WebServer over BSD sockets.
// Handles one request per handleClient() call, exactly like the single
// WebServerTask on the device; handlers block the task the same way.
#include "WebServer.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── tiny helpers ─────────────────────────────────────────────────────────────

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string urlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0)
        {
            out += (char)((hexVal(s[i + 1]) << 4) | hexVal(s[i + 2]));
            i += 2;
        }
        else if (s[i] == '+')
            out += ' ';
        else
            out += s[i];
    }
    return out;
}

static const char* reasonPhrase(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Status";
    }
}

// ── mbedtls/base64 (used by web_ui Basic-auth decoding) ──────────────────────

int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen,
                          const unsigned char* src, size_t slen)
{
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *olen = 0;
    size_t out = 0;
    unsigned group = 0;
    int    bits   = 0;
    size_t i      = 0;
    bool   pad    = false;
    for (; i < slen; i++)
    {
        char c = (char)src[i];
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (c == '=')
        {
            pad = true;
            continue;
        }
        if (pad) return -1;   // data after padding
        const char* hit = strchr(table, c);
        if (!hit || c == '\0') return -1;
        group = (group << 6) | (unsigned)(hit - table);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (out >= dlen) return -1;   // buffer too small
            dst[out++] = (unsigned char)((group >> bits) & 0xFF);
        }
    }
    *olen = out;
    return 0;
}

// ── ESPmDNS stub ─────────────────────────────────────────────────────────────

#include "ESPmDNS.h"
MDNSResponder MDNS;
// (ESP chip-info object already lives in shim/esp_system.h.)

// ── WebServer ────────────────────────────────────────────────────────────────

WebServer::WebServer(int port) : port_(port) {}

WebServer::~WebServer()
{
    if (listen_fd_ >= 0) close(listen_fd_);
}

void WebServer::on(const char* uri, THandlerFunction fn)
{
    routes_.push_back({uri, -1, std::move(fn), nullptr});
}

void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction fn)
{
    routes_.push_back({uri, (int)method, std::move(fn), nullptr});
}

void WebServer::on(const char* uri, HTTPMethod method, THandlerFunction fn, TUploadFunction uploadFn)
{
    routes_.push_back({uri, (int)method, std::move(fn), std::move(uploadFn)});
}

void WebServer::begin()
{
    int port = port_;
    if (const char* env = getenv("INFOCLOCK_HTTP_PORT"))
        port = atoi(env);
    else if (port < 1024)
        port = 8080;   // binding <1024 needs root on the host

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        perror("[WEB] socket");
        return;
    }
    int one = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listen_fd_, 8) != 0)
    {
        perror("[WEB] bind/listen");
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    fcntl(listen_fd_, F_SETFL, fcntl(listen_fd_, F_GETFL, 0) | O_NONBLOCK);
    fprintf(stderr, "[WEB] host prototype: HTTP server on http://localhost:%d\n", port);
}

void WebServer::resetRequestState()
{
    method_ = HTTP_GET;
    args_.clear();
    req_headers_.clear();
    status_ = 0;
    content_type_ = "text/html";
    body_.clear();
    resp_headers_.clear();
    upload_ = HTTPUpload{};
}

void WebServer::parseUrlencoded(const std::string& data)
{
    size_t start = 0;
    while (start <= data.size())
    {
        size_t amp = data.find('&', start);
        std::string pair = data.substr(start, (amp == std::string::npos ? data.size() : amp) - start);
        if (!pair.empty())
        {
            size_t eq = pair.find('=');
            std::string key = (eq == std::string::npos) ? pair : pair.substr(0, eq);
            std::string val = (eq == std::string::npos) ? "" : pair.substr(eq + 1);
            if (!args_.count(urlDecode(key)))
                args_[urlDecode(key)] = urlDecode(val);
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
}

bool WebServer::hasArg(const char* name) const
{
    return args_.count(name) != 0;
}

String WebServer::arg(const char* name) const
{
    auto it = args_.find(name);
    return it == args_.end() ? String("") : String(it->second.c_str());
}

bool WebServer::hasHeader(const char* name) const
{
    for (const auto& h : req_headers_)
        if (strcasecmp(h.first.c_str(), name) == 0) return true;
    return false;
}

String WebServer::header(const char* name) const
{
    for (const auto& h : req_headers_)
        if (strcasecmp(h.first.c_str(), name) == 0) return String(h.second.c_str());
    return String("");
}

void WebServer::sendHeader(const String& name, const String& value, bool first)
{
    if (first)
        resp_headers_.insert(resp_headers_.begin(), {name.c_str(), value.c_str()});
    else
        resp_headers_.emplace_back(name.c_str(), value.c_str());
}

void WebServer::send(int code, const char* content_type, const String& payload)
{
    status_ = code;
    content_type_ = content_type;
    body_ = payload.c_str();
}

void WebServer::send(int code, const char* content_type, const char* payload)
{
    status_ = code;
    content_type_ = content_type;
    body_ = payload ? payload : "";
}

void WebServer::send(int code)
{
    send(code, "text/plain", "");
}

void WebServer::send_P(int code, const char* content_type, const char* payload)
{
    send(code, content_type, payload);
}

void WebServer::sendContent(const String& payload) { body_ += payload.c_str(); }
void WebServer::sendContent(const char* payload) { if (payload) body_ += payload; }

void WebServer::sendContent(const char* payload, size_t len)
{
    if (payload) body_.append(payload, len);
}

void WebServer::sendContent_P(const char* payload) { sendContent(payload); }

void WebServer::handleClient()
{
    if (listen_fd_ < 0) return;

    sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listen_fd_, (sockaddr*)&ss, &slen);
    if (fd < 0) return;   // nothing pending — non-blocking accept

    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    resetRequestState();

    // ── read the request (headers, then Content-Length body bytes) ──────────
    std::string data;
    char buf[4096];
    size_t headerEnd = std::string::npos;
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        data.append(buf, (size_t)n);
        if (data.size() > 256 * 1024) break;   // absurd request cap
    }

    if (headerEnd == std::string::npos)
    {
        close(fd);
        return;
    }

    std::string head = data.substr(0, headerEnd);
    std::string rest = data.substr(headerEnd + 4);

    // request line: METHOD SP TARGET SP VERSION
    size_t sp1 = head.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : head.find(' ', sp1 + 1);
    std::string methodStr = (sp1 != std::string::npos) ? head.substr(0, sp1) : "GET";
    std::string target    = (sp1 != std::string::npos && sp2 != std::string::npos)
                                ? head.substr(sp1 + 1, sp2 - sp1 - 1) : "/";
    method_ = (methodStr == "POST") ? HTTP_POST : HTTP_GET;

    // headers (case-insensitive lookup later)
    {
        size_t start = head.find("\r\n");
        start = (start == std::string::npos) ? head.size() : start + 2;
        while (start < head.size())
        {
            size_t eol = head.find("\r\n", start);
            std::string line = head.substr(start, (eol == std::string::npos ? head.size() : eol) - start);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                size_t vstart = colon + 1;
                while (vstart < line.size() && line[vstart] == ' ') vstart++;
                req_headers_.emplace_back(line.substr(0, colon), line.substr(vstart));
            }
            if (eol == std::string::npos) break;
            start = eol + 2;
        }
    }

    size_t contentLen = 0;
    for (const auto& h : req_headers_)
        if (strcasecmp(h.first.c_str(), "Content-Length") == 0)
            contentLen = (size_t)strtoul(h.second.c_str(), nullptr, 10);
    while (rest.size() < contentLen && rest.size() <= 256 * 1024)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        rest.append(buf, (size_t)n);
    }

    // ── split query string / form body into args ────────────────────────────
    size_t q = target.find('?');
    if (q != std::string::npos)
    {
        parseUrlencoded(target.substr(q + 1));
        target = target.substr(0, q);
    }
    for (const auto& h : req_headers_)
        if (strcasecmp(h.first.c_str(), "Content-Type") == 0 &&
            h.second.find("application/x-www-form-urlencoded") != std::string::npos)
            parseUrlencoded(rest);

    // ── dispatch ────────────────────────────────────────────────────────────
    const Route* route = nullptr;
    for (const auto& r : routes_)
        if (r.uri == target && (r.method == -1 || r.method == (int)method_))
        {
            route = &r;
            break;
        }

    if (!route)
        send(404, "text/plain", "Not Found");
    else
        route->fn();

    if (status_ == 0)
        send(200, "text/html", "");

    // ── write the buffered response ─────────────────────────────────────────
    std::string out = "HTTP/1.1 " + std::to_string(status_) + " " +
                      reasonPhrase(status_) + "\r\n";
    out += "Content-Type: " + content_type_ + "\r\n";
    out += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    out += "Connection: close\r\n";
    for (const auto& h : resp_headers_)
        out += h.first + ": " + h.second + "\r\n";
    out += "\r\n";
    out += body_;

    size_t sent = 0;
    while (sent < out.size())
    {
        ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
    close(fd);
}
