// http_utils.cpp
//
// Simple HTTP(S) helper for ESP32 / PlatformIO projects.
// Provides a small wrapper to perform GET requests and return response body and status.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <memory>

namespace HttpUtils {

/// Perform an HTTP(S) GET.
/// @param url        Full URL (http:// or https://)
/// @param outBody    Will be filled with response body on success (empty on failure)
/// @param insecure   If true and using HTTPS, the TLS certificate will not be verified (useful for testing)
/// @return HTTP status code (>0) on success, or a negative value on error
int httpGet(const String &url, String &outBody, bool insecure = true) {
    outBody = String();

    if (url.length() == 0) {
        return -1;
    }

    HTTPClient http;
    std::unique_ptr<WiFiClient> plainClient;
    std::unique_ptr<WiFiClientSecure> secureClient;
    int result = -1;

    // Choose client based on scheme
    if (url.startsWith("https://")) {
        secureClient = std::unique_ptr<WiFiClientSecure>(new WiFiClientSecure());
        if (insecure) {
            // Skip certificate validation (NOT recommended for production)
            secureClient->setInsecure();
        }
        http.begin(*secureClient, url);
    } else {
        plainClient = std::unique_ptr<WiFiClient>(new WiFiClient());
        http.begin(*plainClient, url);
    }

    // Perform GET
    int httpCode = http.GET();
    if (httpCode > 0) {
        // success, read response
        outBody = http.getString();
        result = httpCode;
    } else {
        // error during connection/request
        result = -httpCode; // return negative of error (<=0)
    }

    http.end();

    // unique_ptr cleans up automatically
    return result;
}

} // namespace HttpUtils

// Example usage (commented):
// String body;
// int status = HttpUtils::httpGet("https://example.com/data.json", body, true);
// if (status > 0) { Serial.printf("HTTP %d, len=%d\n", status, body.length()); }
