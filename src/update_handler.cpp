#include <Arduino.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <logger.hpp>
#include <task_registry.hpp>
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
                               "<form id='uf' method='POST' action='/update' enctype='multipart/form-data'>"
                               "<input type='file' name='firmware' accept='.bin' "
                               "style='display:block;margin-bottom:14px'>"
                               "<button class='btn btn-primary' type='submit'>&#9654; Flash firmware</button>"
                               "</form>"
                               "<div id='pw' style='display:none;margin-top:16px'>"
                               "<div style='background:#e2e8f0;border-radius:6px;height:18px;overflow:hidden'>"
                               "<div id='pb' style='background:#2563eb;height:100%;width:0'></div></div>"
                               "<p id='pt' style='color:#475569;margin-top:8px'></p></div>"
                               "</div>"
                               "<script>"
                               "var f=document.getElementById('uf');"
                               "f.onsubmit=function(e){"
                               "if(!f.firmware.files.length)return false;"
                               "e.preventDefault();"
                               "var b=f.querySelector('button'),pb=document.getElementById('pb'),"
                               "pt=document.getElementById('pt'),x=new XMLHttpRequest();"
                               "b.disabled=true;document.getElementById('pw').style.display='block';"
                               "pt.textContent='Uploading... 0%';"
                               "x.upload.onprogress=function(v){if(v.lengthComputable){"
                               "var p=Math.round(100*v.loaded/v.total);pb.style.width=p+'%';"
                               "pt.textContent=p<100?'Uploading... '+p+'%':'Writing to flash, please wait...'}};"
                               "x.onload=function(){document.open();document.write(x.responseText);document.close()};"
                               "x.onerror=function(){pt.textContent='Connection lost - the device may be rebooting. Check /status.';b.disabled=false};"
                               "x.open('POST','/update');x.send(new FormData(f));return false};"
                               "</script>"));
    sendPageFoot();
}

static bool otaRunning = false;
static String otaError;
static bool otaFirstChunk = false;

// All tasks share priority 1; the web server must drain the socket faster
// than the 5 s HTTP_MAX_POST_WAIT, so it outranks them while an upload runs.
static void otaBoost(bool on)
{
    vTaskPrioritySet(nullptr, on ? 2 : 1);
}

static void otaFail(const String& why)
{
    if (otaError.length() == 0) otaError = why;
    if (otaRunning) Update.abort();
    otaRunning = false;
    otaBoost(false);
    logPrintf("WEB", "OTA failed: %s", otaError.c_str());
}

void handle_update_upload()
{
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        otaError = "";
        otaRunning = false;
        otaFirstChunk = true;
        otaBoost(true);
        if (!check_auth_header())
        {
            otaError = "Authentication failed";
            logPrintf("WEB", "OTA rejected: %s", otaError.c_str());
        }
        else if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            otaError = String("Update.begin: ") + Update.errorString();
            logPrintf("WEB", "OTA rejected: %s", otaError.c_str());
        }
        else
        {
            otaRunning = true;
            logPrintf("WEB", "OTA start: %s", upload.filename.c_str());
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        // handleClient() blocks for the whole upload; keep the supervisor fed
        task_heartbeat();
        if (!otaRunning) return;

        // esp_app_desc_t magic (0xABCD5432) sits at offset 32 of an app image;
        // a bootloader or merged image has something else there.
        if (otaFirstChunk)
        {
            otaFirstChunk = false;
            static const uint8_t magic[4] = {0x32, 0x54, 0xCD, 0xAB};
            if (upload.currentSize < 36 || upload.buf[0] != 0xE9 ||
                memcmp(upload.buf + 32, magic, 4) != 0)
            {
                otaFail("Not an application image - use the plain .bin, not -merged.bin");
                return;
            }
        }
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            otaFail(String("Write failed: ") + Update.errorString());
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        otaBoost(false);
        if (otaRunning)
        {
            otaRunning = false;
            if (Update.end(true))
                logPrintf("WEB", "OTA complete: %u bytes", upload.totalSize);
            else
            {
                otaError = String("Update.end: ") + Update.errorString();
                logPrintf("WEB", "OTA end error: %s", otaError.c_str());
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        otaFail("Upload aborted");
    }
}

void handle_update_post()
{
    otaBoost(false);
    if (!is_authenticated()) return;

    bool ok = otaError.length() == 0 && !Update.hasError();
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
        server.sendContent(otaError.length() ? otaError : String(Update.errorString()));
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
