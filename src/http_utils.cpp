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

/// Perform an HTTP(S) GET.
/// @param url        Full URL (http:// or https://)
/// @param outBody    Will be filled with response body on success (empty on failure)
/// @param insecure   When true, skip TLS certificate verification (for https://)
/// @return HTTP status code (>0) on success, or a negative value on error
int httpGet(const String &url, String &outBody, bool insecure,
            const std::vector<std::pair<String, String>> &headers) {
    outBody = String();

    if (url.length() == 0) {
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

    for (const auto &h : headers)
        http.addHeader(h.first, h.second);

    int httpCode = http.GET();
    if (httpCode > 0) {
        outBody = http.getString();
        if (httpCode != 200) {
            // Log URL + first 100 chars of the body so 4xx/5xx errors are
            // immediately visible in the web log without a serial monitor.
            logPrintf("HTTP", "GET %s -> %d: %.100s",
                      redactUrl(url.c_str()).c_str(), httpCode, outBody.c_str());
        }
    } else {
        logPrintf("HTTP", "GET %s -> err %d", redactUrl(url.c_str()).c_str(), httpCode);
    }

    http.end();
    return httpCode;
}

} // namespace HttpUtils
