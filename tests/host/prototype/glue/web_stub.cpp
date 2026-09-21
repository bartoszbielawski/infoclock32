// Host stubs for the OTA endpoints — the ESP32 flash-update stack has no
// meaning on the host prototype, but the routes must exist so /update
// responds instead of 404. update_handler.cpp is not compiled here.
#include <Arduino.h>
#include <web_ui.hpp>

void handle_update_get()
{
    server.send(200, "text/plain",
                "OTA upload is not available on the host prototype.\n");
}

void handle_update_post() {}

void handle_update_upload() {}
