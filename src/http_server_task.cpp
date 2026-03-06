#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>

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

// Web server task
void web_server_task(void* pvParameters) {    
    // Start the server
    server.on("/", handleRoot);
    server.on("/edit", handle_file_edit);
    server.begin();

    // Main server loop
    while (true)
    {
        server.handleClient();
        vTaskDelay(10 / portTICK_PERIOD_MS); // Yield to other tasks
    }
}

// Simple authentication function
bool is_authenticated() {
    if (!server.hasHeader("Authorization")) {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"Secure Area\"");
        server.send(401, "text/plain", "Unauthorized");
        return false;
    }

    String authHeader = server.header("Authorization");
    if (authHeader.startsWith("Basic ")) {
        String credentials = authHeader.substring(6); // Extract the part after "Basic "
        
        // Split the credentials into username and password
        int colonIndex = credentials.indexOf(':');
        if (colonIndex != -1) {
            String username = credentials.substring(0, colonIndex);
            String password = credentials.substring(colonIndex + 1);

            // Replace "admin" and "password" with your desired credentials
            if (username == "admin" && password == "password") {
                return true;
            }
        }
    }

    server.sendHeader("WWW-Authenticate", "Basic realm=\"Secure Area\"");
    server.send(401, "text/plain", "Unauthorized");
    return false;
}

// Constant for the filename
const char* FILENAME = "/config.txt";

// Function to handle file display and editing
void handle_file_edit() {
    if (!is_authenticated()) {
        return;
    }

    if (server.method() == HTTP_GET) {
        // Read the file from LittleFS
        File file = LittleFS.open(FILENAME, "r");
        if (!file) {
            server.send(500, "text/plain", "Failed to open file");
            return;
        }

        String fileContent;
        while (file.available()) {
            fileContent += (char)file.read();
        }
        file.close();

        // Send the file content in an HTML form
        String html = "<form method='POST' action='/edit'><textarea name='content' rows='20' cols='80'>";
        html += fileContent;
        html += "</textarea><br><input type='submit' value='Save'></form>";
        server.send(200, "text/html", html);
        return;
    }
    if (server.method() == HTTP_POST) 
    {
        // Save the modified content back to the file
        if (!server.hasArg("content")) {
            server.send(400, "text/plain", "Bad Request");
            return;
        }

        String newContent = server.arg("content");
        File file = LittleFS.open(FILENAME, "w");
        if (!file) {
            server.send(500, "text/plain", "Failed to open file for writing");
            return;
        }

        file.print(newContent);
        file.close();

        server.send(200, "text/plain", "File updated successfully");
        return;
    }

    // Other cases
    server.send(405, "text/plain", "Method Not Allowed");
}
