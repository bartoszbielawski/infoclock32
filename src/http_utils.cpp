// http_utils.cpp
//
// Simple HTTP(S) helper for ESP32 / PlatformIO projects.
// Uses the HTTPClient 1.1-compatible API (begin(WiFiClient&, url)).
// HTTPS is handled by passing a WiFiClientSecure; insecure=true skips cert verification.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <vector>
#include <utility>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <http_utils.hpp>
#if __has_include(<WiFiClientSecure.h>)
  #include <WiFiClientSecure.h>
  #define HAS_WIFI_CLIENT_SECURE
#endif
#include <logger.hpp>
#include <data_store.hpp>
#include <config_utils.hpp>

// Mozilla root CAs (certs/x509_crt_bundle.bin, embedded via
// board_build.embed_files). Used when the config key tls_verify=1.
#if defined(ESP_PLATFORM) && defined(HAS_WIFI_CLIENT_SECURE)
  extern const uint8_t ca_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");
  #define HAS_CA_BUNDLE 1
#endif

namespace HttpUtils {

// One request at a time, device-wide. A TLS handshake needs ~40 KB of heap
// in one go; weather, LHC, menu and push fetches starting together ran the
// heap down to a few KB and failed with "SSL - Memory allocation failed".
// Requests take seconds, so a waiting task just starts a little later.
static constexpr TickType_t kRequestWaitTicks = 45000 / portTICK_PERIOD_MS;

static SemaphoreHandle_t requestMutex()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();  // thread-safe static init
    return m;
}

namespace {
struct RequestLock {
    bool held;
    RequestLock() : held(xSemaphoreTake(requestMutex(), kRequestWaitTicks) == pdTRUE) {}
    ~RequestLock() { if (held) xSemaphoreGive(requestMutex()); }
};

// The response body as a Stream for a streaming parser. It reads the client in
// chunks and, while waiting for data, sleeps a tick instead of spinning:
// Arduino's Stream::timedRead() busy-waits, and on the single-core C3 a TLS
// read stalled for more than 5 s starved the idle task until the task watchdog
// reset the chip. It also counts the bytes consumed (a body cut short shows as
// "IncompleteInput" in the parser) and records the heap low point while reading.
class BodyStream : public Stream {
public:
    BodyStream(WiFiClient &c, uint32_t timeoutMs) : c_(c), timeoutMs_(timeoutMs) {}
    int available() override { return (int)(len_ - pos_) + max(0, c_.available()); }
    int read() override { return fill() ? (++count, buf_[pos_++]) : -1; }
    int peek() override { return fill() ? buf_[pos_] : -1; }
    size_t readBytes(char *out, size_t n) override {
        size_t got = 0;
        while (got < n && fill()) {
            size_t k = min(n - got, len_ - pos_);
            memcpy(out + got, buf_ + pos_, k);
            pos_ += k; got += k;
        }
        count += got;
        return got;
    }
    size_t write(uint8_t) override { return 0; }
    void flush() override {}

    size_t count = 0;
    uint32_t minFree = UINT32_MAX;     // lowest free heap seen while reading
    uint32_t minLargest = UINT32_MAX;  // smallest "largest block" seen

private:
    // Make sure the buffer holds at least one byte; false on timeout / close.
    bool fill() {
        if (pos_ < len_) return true;
        const uint32_t start = millis();
        for (;;) {
            const int avail = c_.available();
            if (avail > 0) {
                const int r = c_.read(buf_, min((size_t)avail, sizeof(buf_)));
                if (r > 0) { pos_ = 0; len_ = (size_t)r; sample(); return true; }
            } else if (!c_.connected()) {
                return false;
            }
            if (millis() - start >= timeoutMs_) return false;
            vTaskDelay(1);   // yield: let the idle task (and its watchdog) run
        }
    }
    void sample() {
        uint32_t f = esp_get_free_heap_size();
        uint32_t l = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        if (f < minFree) minFree = f;
        if (l < minLargest) minLargest = l;
    }
    WiFiClient &c_;
    const uint32_t timeoutMs_;
    uint8_t buf_[256];
    size_t pos_ = 0, len_ = 0;
};
}

// One request path for both body modes: `outBody` (buffered) or `onBody`
// (streamed). The clients live here so they outlive the body handler.
static int doGet(const String &url, bool insecure,
                 const std::vector<std::pair<String, String>> &headers,
                 String *outBody, const std::function<void(Stream &)> *onBody) {
    if (outBody) *outBody = String();

    if (url.length() == 0) {
        return -1;
    }

    // Declared before the clients so it is released after they free their
    // TLS buffers (destructors run in reverse order).
    RequestLock lock;
    if (!lock.held) {
        logPrintf("HTTP", "GET %s skipped: another request held the line for 45 s",
                  redactUrl(url.c_str()).c_str());
        return -1;
    }

    HTTPClient http;
    WiFiClient plainClient;
#ifdef HAS_WIFI_CLIENT_SECURE
    WiFiClientSecure secureClient;
#endif
    bool ok;

    if (url.startsWith("https")) {
#ifdef HAS_WIFI_CLIENT_SECURE
        // tls_verify=1 checks the server certificate against the embedded
        // root CAs; otherwise the caller's `insecure` flag decides (the
        // historical default skips verification).
        bool verify = DataStore::getInstance().get_value<int>("tls_verify", 0) == 1;
#ifdef HAS_CA_BUNDLE
        if (verify)
            secureClient.setCACertBundle(ca_bundle_start);
        else
#else
        (void)verify;
#endif
        if (insecure)
            secureClient.setInsecure();
        secureClient.setHandshakeTimeout(15);   // seconds; the default is 120
        ok = http.begin(secureClient, url);
#else
        logPrintf("HTTP", "HTTPS not supported on this build (no WiFiClientSecure)");
        ok = http.begin(plainClient, url);
#endif
    } else {
        ok = http.begin(plainClient, url);
    }

    if (!ok) {
        logPrintf("HTTP", "http.begin() failed for %s", redactUrl(url.c_str()).c_str());
        return -1;
    }

    http.setConnectTimeout(8000);
    http.setTimeout(10000);

    for (const auto &h : headers)
        http.addHeader(h.first, h.second);

    // Streaming needs the plain body: HTTP/1.0 rules out chunked encoding.
    if (onBody) http.useHTTP10(true);

    const uint32_t heapBefore = esp_get_free_heap_size();
    int httpCode = http.GET();
    if (httpCode == 200 && onBody) {
        const int expected = http.getSize();   // -1 when the server sent no length
        const uint32_t heapHeaders = esp_get_free_heap_size();
        const uint32_t t0 = millis();
        // The body reader owns the read timeout: http.setTimeout() does not
        // reach the client's Stream timeout (it stays at the 1 s default on
        // WiFiClientSecure), and a parser reading a 20 KB body gave up at
        // the first 1 s pause in the transfer ("IncompleteInput").
        BodyStream counted(http.getStream(), 10000);
        (*onBody)(counted);
        if (expected > 0 && counted.count < (size_t)expected) {
            char why[96] = "";
#ifdef HAS_WIFI_CLIENT_SECURE
            if (url.startsWith("https") && secureClient.lastError(why, sizeof(why)) == 0)
                why[0] = '\0';
#endif
            logPrintf("HTTP", "GET %s: body cut at %u of %d bytes after %lu ms "
                      "(%s; connected %d, heap %u, largest %u)",
                      redactUrl(url.c_str()).c_str(), (unsigned)counted.count, expected,
                      (unsigned long)(millis() - t0), why[0] ? why : "no TLS error",
                      (int)http.connected(),
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
        // Heap profile of the fetch: what the connection costs vs. the body.
        logPrintf("HTTP", "GET %s: %u B in %lu ms; heap before %u, after headers %u, "
                  "min while reading %u (largest %u)",
                  redactUrl(url.c_str()).c_str(), (unsigned)counted.count,
                  (unsigned long)(millis() - t0), (unsigned)heapBefore, (unsigned)heapHeaders,
                  (unsigned)counted.minFree, (unsigned)counted.minLargest);
    } else if (httpCode > 0) {
        String body = http.getString();
        if (httpCode == 200 && body.length() == 0 && http.getSize() > 0) {
            logPrintf("HTTP", "GET %s -> 200 but the %d-byte body did not fit in memory",
                      redactUrl(url.c_str()).c_str(), http.getSize());
        }
        if (httpCode != 200) {
            // Log URL + first 100 chars of the body so 4xx/5xx errors are
            // immediately visible in the web log without a serial monitor.
            logPrintf("HTTP", "GET %s -> %d: %.100s",
                      redactUrl(url.c_str()).c_str(), httpCode, body.c_str());
        }
        if (outBody) *outBody = body;
    } else {
        // Say why: a TLS handshake needs one large heap block, and mbedTLS
        // reports its own reason (DNS failures show up as a plain -1 there).
        char why[96] = "";
#ifdef HAS_WIFI_CLIENT_SECURE
        if (url.startsWith("https") && secureClient.lastError(why, sizeof(why)) == 0)
            why[0] = '\0';
#endif
        logPrintf("HTTP", "GET %s -> err %d (%s; heap %u free, largest block %u)",
                  redactUrl(url.c_str()).c_str(), httpCode, why[0] ? why : "no TLS error",
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    http.end();
    return httpCode;
}

/// Perform an HTTP(S) GET.
/// @param url        Full URL (http:// or https://)
/// @param outBody    Will be filled with response body on success (empty on failure)
/// @param insecure   When true, skip TLS certificate verification (for https://)
/// @return HTTP status code (>0) on success, or a negative value on error
int httpGet(const String &url, String &outBody, bool insecure,
            const std::vector<std::pair<String, String>> &headers) {
    return doGet(url, insecure, headers, &outBody, nullptr);
}

int httpGetStream(const String &url, const std::function<void(Stream &)> &onBody,
                  bool insecure, const std::vector<std::pair<String, String>> &headers) {
    return doGet(url, insecure, headers, nullptr, &onBody);
}

} // namespace HttpUtils
