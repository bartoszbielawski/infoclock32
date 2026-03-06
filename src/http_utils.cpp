// http_utils.cpp
//
// Simple HTTP(S) helper for ESP32 / PlatformIO projects.
// Uses the HTTPClient 1.1-compatible API (begin(WiFiClient&, url)).
// Note: HTTPS support depends on the ESP-IDF TLS layer being invoked
// by HTTPClient internally when it detects an https:// URL.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

namespace HttpUtils {

/// Perform an HTTP(S) GET.
/// @param url        Full URL (http:// or https://)
/// @param outBody    Will be filled with response body on success (empty on failure)
/// @param insecure   Reserved for future cert-pinning support; currently ignored
/// @return HTTP status code (>0) on success, or a negative value on error
int httpGet(const String &url, String &outBody, bool insecure) {
    outBody = String();

    if (url.length() == 0) {
        return -1;
    }

    HTTPClient http;
    WiFiClient client;

    if (!http.begin(client, url)) {
        Serial.printf("HttpUtils: http.begin() failed for %s\n", url.c_str());
        return -1;
    }

    int httpCode = http.GET();
    if (httpCode > 0) {
        outBody = http.getString();
    } else {
        Serial.printf("HttpUtils: GET failed, code=%d\n", httpCode);
    }

    http.end();
    return httpCode;
}

} // namespace HttpUtils
