#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <data_store.hpp>
#include <logger.hpp>

// Web server instance
WebServer server(80);

// Accessor for the server object
WebServer* getWebServer() {
    return &server;
}

// Handle root URL
void handleRoot() {
    server.send(200, "text/plain", "Hello, world!");
}

void handle_file_edit();
void handle_reboot();
void handle_log();

// Web server task
void web_server_task(void* pvParameters) {
    server.on("/", handleRoot);
    server.on("/edit", handle_file_edit);
    server.on("/reboot", HTTP_POST, handle_reboot);
    server.on("/log", HTTP_GET, handle_log);
    server.begin();

    while (true)
    {
        server.handleClient();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Decode and verify HTTP Basic auth credentials.
// Returns true if username=="admin" and password=="password".
bool is_authenticated() {
    auto reject = [&]() {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Secure Area\"");
        server.send(401, "text/plain", "Unauthorized");
    };

    if (!server.hasHeader("Authorization")) { reject(); return false; }

    String authHeader = server.header("Authorization");
    if (!authHeader.startsWith("Basic ")) { reject(); return false; }

    // Decode the base64 payload ("Basic <b64(user:pass)>")
    String encoded = authHeader.substring(6);
    unsigned char decoded[64] = {};
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                              (const unsigned char*)encoded.c_str(), encoded.length()) != 0)
    {
        reject(); return false;
    }
    decoded[decoded_len] = '\0';

    String credentials = String((char*)decoded);
    int colon = credentials.indexOf(':');
    if (colon == -1) { reject(); return false; }

    if (credentials.substring(0, colon)  == "admin" &&
        credentials.substring(colon + 1) == "password")
    {
        return true;
    }

    reject();
    return false;
}

// Constant for the filename
const char* FILENAME = "/config.txt";

// Function to handle file display and editing
void handle_file_edit() {
    if (!is_authenticated()) return;

    LittleFS.begin(true);

    if (server.method() == HTTP_GET) {
        File file = LittleFS.open(FILENAME, "r");
        if (!file) {
            server.send(500, "text/plain", "Failed to open file");
            return;
        }

        String fileContent;
        while (file.available()) fileContent += (char)file.read();
        file.close();

        String html = "<form method='POST' action='/edit'>"
                      "<textarea name='content' rows='20' cols='80'>";
        html += fileContent;
        html += "</textarea><br><input type='submit' value='Save'></form>";
        server.send(200, "text/html", html);
        return;
    }

    if (server.method() == HTTP_POST) {
        if (!server.hasArg("content")) {
            server.send(400, "text/plain", "Bad Request");
            return;
        }

        File file = LittleFS.open(FILENAME, "w");
        if (!file) {
            server.send(500, "text/plain", "Failed to open file for writing");
            return;
        }
        file.print(server.arg("content"));
        file.close();

        // Reload config so changes take effect immediately without a reboot
        DataStore::getInstance().load_from_file(FILENAME);

        server.send(200, "text/plain", "Saved. Config reloaded.");
        return;
    }

    server.send(405, "text/plain", "Method Not Allowed");
}

void handle_reboot() {
    if (!is_authenticated()) return;
    server.send(200, "text/plain", "Rebooting...");
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP.restart();
}

void handle_log() {
    if (!is_authenticated()) return;

    String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta http-equiv='refresh' content='5'>"
                    "<title>Log</title></head><body>"
                    "<h2>Log <a href='/log'>[refresh]</a></h2>"
                    "<table border='1' cellpadding='4' style='font-family:monospace'>");

    const auto &history = getLogHistory();
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        html += F("<tr><td>");
        html += *it;
        html += F("</td></tr>\n");
    }

    html += F("</table></body></html>");
    server.send(200, "text/html", html);
}
