#include <Arduino.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <logger.hpp>
#include <reboot_utils.hpp>
#include <web_ui.hpp>

void handle_update_get()
{
    if (!is_authenticated()) return;

    sendPageHead("Firmware update");
    sendPageNav("/update");
    server.sendContent_P(PSTR("<h2>&#128190; Firmware update</h2>"
                               "<div class='card'>"
                               "<p style='color:#475569;margin-bottom:16px'>"
                               "Upload a compiled <code>.bin</code> file to flash new firmware. "
                               "The device will reboot automatically on success.</p>"
                               "<form method='POST' action='/update' enctype='multipart/form-data'>"
                               "<input type='file' name='firmware' accept='.bin' "
                               "style='display:block;margin-bottom:14px'>"
                               "<button class='btn btn-primary' type='submit'>&#9654; Flash firmware</button>"
                               "</form></div>"));
    sendPageFoot();
}

static bool otaRunning = false;

void handle_update_upload()
{
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        otaRunning = check_auth_header() && Update.begin(UPDATE_SIZE_UNKNOWN);
        if (otaRunning)
            logPrintf("WEB", "OTA start: %s", upload.filename.c_str());
        else
            logPrintf("WEB", "OTA rejected (auth or begin failed)");
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (otaRunning)
            Update.write(upload.buf, upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (otaRunning)
        {
            otaRunning = false;
            if (Update.end(true))
                logPrintf("WEB", "OTA complete: %u bytes", upload.totalSize);
            else
                logPrintf("WEB", "OTA end error: %s", Update.errorString());
        }
    }
}

void handle_update_post()
{
    if (!is_authenticated()) return;

    bool ok = !Update.hasError();
    sendPageHead(ok ? "Update complete" : "Update failed");
    sendPageNav("/update");
    if (ok)
    {
        server.sendContent_P(PSTR("<h2>&#10003; Update complete</h2>"
                                   "<div class='card'>"
                                   "<p style='color:#15803d'>Firmware flashed successfully. Rebooting&hellip;</p>"
                                   "</div>"));
    }
    else
    {
        server.sendContent_P(PSTR("<h2>&#9888; Update failed</h2>"
                                   "<div class='card'>"
                                   "<p style='color:#dc2626'>Error: "));
        server.sendContent(Update.errorString());
        server.sendContent_P(PSTR("</p><div class='actions'>"
                                   "<a class='btn btn-primary' href='/update'>&#8592; Try again</a>"
                                   "</div></div>"));
    }
    sendPageFoot();

    if (ok)
    {
        reboot_with_message();
    }
}
