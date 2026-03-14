#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <version.hpp>
#include <mqtt_task.h>
#include <web_ui.hpp>

// Handlers defined in other translation units
void handle_actions();
void handle_messages();
void handle_update_get();
void handle_update_post();
void handle_update_upload();

// ── / (home dashboard) ────────────────────────────────────────────────────────

void handle_home()
{
    unsigned long ms = millis();
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%luh %lum %lus",
             ms / 3600000UL, (ms % 3600000UL) / 60000UL, (ms % 60000UL) / 1000UL);

    char heap[24];
    snprintf(heap, sizeof(heap), "%u KB (%u B)",
             (unsigned)esp_get_free_heap_size() / 1024,
             (unsigned)esp_get_free_heap_size());

    char rssiStr[20];
    int rssi = WiFi.RSSI();
    const char* quality = rssi >= -60 ? "excellent" : rssi >= -70 ? "good"
                        : rssi >= -80 ? "fair" : "weak";
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm (%s)", rssi, quality);

    auto& ds = DataStore::getInstance();
    std::string mqttServer = ds.get_value("mqtt_server", "");
    char mqttStatus[96];
    if (mqttServer.empty())
        snprintf(mqttStatus, sizeof(mqttStatus),
                 "<span style='color:#94a3b8'>not configured</span>");
    else if (mqtt_is_connected())
        snprintf(mqttStatus, sizeof(mqttStatus),
                 "&#10003; connected to %s", mqttServer.c_str());
    else
        snprintf(mqttStatus, sizeof(mqttStatus),
                 "&#9888; disconnected (%s)", mqttServer.c_str());

    char brightStr[4];
    snprintf(brightStr, sizeof(brightStr), "%d",
             ds.get_value<int>("brightness", 7));

    sendPageHead("Home", "<meta http-equiv='refresh' content='30'>");
    sendPageNav("/");

    server.sendContent_P(PSTR("<h2>&#9881;&#65039; Device status</h2>"
                               "<table style='margin-bottom:24px'>"
                               "<tr><th colspan='2'>&#127760; Network</th></tr>"));
    sendRow("IP address", WiFi.localIP().toString().c_str());
    sendRow("Hostname",   WiFi.getHostname());
    sendRow("SSID",       WiFi.SSID().c_str());
    sendRow("Signal",     rssiStr);
    server.sendContent_P(PSTR("<tr><th colspan='2'>&#128421;&#65039; System</th></tr>"));
    sendRow("Firmware",   APP_VERSION " &bull; built " BUILD_DATE);
    sendRow("Uptime",     uptime);
    sendRow("Free heap",  heap);
    sendRow("Chip",       ESP.getChipModel());
    server.sendContent_P(PSTR("<tr><th colspan='2'>&#128225; MQTT</th></tr>"));
    sendRow("Status",     mqttStatus);
    server.sendContent_P(PSTR("</table>"
                               "<h2>&#9889;&#65039; Actions</h2>"
                               "<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128172; Push message</h3>"
                               "<form method='POST' action='/actions'>"
                               "<input type='hidden' name='action' value='push'>"
                               "<input name='message' type='text' placeholder='Message to scroll&hellip;' "
                               "style='width:100%;padding:8px 10px;border:1px solid #cbd5e1;border-radius:6px;"
                               "font-size:.9rem;margin-bottom:10px;background:#fff;color:#1e293b'>"
                               "<button class='btn btn-primary' type='submit'>&#9654; Send</button>"
                               "</form></div>"
                               "<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#9728;&#65039; Brightness (0&ndash;15)</h3>"
                               "<form method='POST' action='/actions'>"
                               "<input type='hidden' name='action' value='brightness'>"
                               "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
                               "<input name='level' type='range' min='0' max='15' value='"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("' style='flex:1' oninput='this.nextElementSibling.textContent=this.value'>"
                               "<span style='font-family:monospace;min-width:2ch'>"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("</span></div>"
                               "<button class='btn btn-primary' type='submit'>Set</button>"
                               "</form></div>"
                               "<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128260; Reboot</h3>"
                               "<form method='POST' action='/reboot'>"
                               "<button class='btn btn-danger' type='submit'>Reboot device</button>"
                               "</form></div>"));
    sendPageFoot();
}

// ── /status ───────────────────────────────────────────────────────────────────

void handle_status()
{
    unsigned long ms = millis();
    char uptime[32];
    snprintf(uptime, sizeof(uptime), "%luh %lum %lus",
             ms / 3600000UL, (ms % 3600000UL) / 60000UL, (ms % 60000UL) / 1000UL);

    char heap[24];
    snprintf(heap, sizeof(heap), "%u KB  (%u B)",
             (unsigned)esp_get_free_heap_size() / 1024,
             (unsigned)esp_get_free_heap_size());

    char rssiStr[20];
    int rssi = WiFi.RSSI();
    const char* quality = rssi >= -60 ? "excellent" : rssi >= -70 ? "good"
                        : rssi >= -80 ? "fair" : "weak";
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm (%s)", rssi, quality);

    sendPageHead("Status", "<meta http-equiv='refresh' content='10'>");
    sendPageNav("/status");
    server.sendContent_P(PSTR("<h2>Device status</h2><table>"
                               "<tr><th colspan='2'>&#127760; Network</th></tr>"));
    sendRow("IP address",  WiFi.localIP().toString().c_str());
    sendRow("Hostname",    WiFi.getHostname());
    sendRow("SSID",        WiFi.SSID().c_str());
    sendRow("Signal",      rssiStr);
    sendRow("MAC address", WiFi.macAddress().c_str());
    server.sendContent_P(PSTR("<tr><th colspan='2'>&#9881;&#65039; System</th></tr>"));
    sendRow("Firmware",    APP_VERSION " &bull; built " BUILD_DATE " " BUILD_TIME);
    sendRow("Uptime",      uptime);
    sendRow("Free heap",   heap);
    sendRow("Chip",        ESP.getChipModel());

    auto rtSnap = RuntimeStore::getInstance().snapshot();
    if (!rtSnap.empty())
    {
        server.sendContent_P(PSTR("<tr><th colspan='2'>&#9889;&#65039; Runtime values</th></tr>"));
        for (const auto& kv : rtSnap)
            sendRow(kv.first.c_str(), kv.second.c_str());
    }

    server.sendContent_P(PSTR("</table>"));
    sendPageFoot();
}

// ── /log ──────────────────────────────────────────────────────────────────────

// Polling script: reads _logSince (set in the page head) and appends new rows
// every 3 s without reloading the page.
static const char LOG_POLL_JS[] PROGMEM = R"js(<script>(function(){
function p(){fetch('/log/entries?since='+_logSince).then(function(r){return r.ok?r.json():Promise.reject();}).then(function(d){
if(d.entries)d.entries.forEach(function(e){
var t=document.querySelector('table'),r=t.insertRow(1);
var c0=r.insertCell(0);c0.className='mono';c0.style.whiteSpace='nowrap';c0.textContent=e.ts;
var c1=r.insertCell(1);var s=document.createElement('span');s.className='tag tag-info';s.textContent=e.tag;c1.appendChild(s);
var c2=r.insertCell(2);c2.className='mono';c2.textContent=e.msg;});
if(d.seq!==undefined)_logSince=d.seq;}).catch(function(){}).then(function(){setTimeout(p,3000);});}
p();})();</script>)js";

void handle_log()
{
    if (!is_authenticated()) return;

    // Embed the current high-water seq so the first poll only fetches new entries.
    char extraHead[64];
    snprintf(extraHead, sizeof(extraHead), "<script>var _logSince=%u;</script>", getLogSeq());

    sendPageHead("Log", extraHead);
    sendPageNav("/log");
    server.sendContent_P(PSTR("<h2>Log <small style='font-weight:400;color:#94a3b8'>"
                               "(newest first, last 40 entries, live)</small></h2>"
                               "<table><tr><th>Timestamp</th><th>Tag</th><th>Message</th></tr>\n"));

    const auto& history = getLogHistory();
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        const String& line = it->line;
        int tagOpen  = line.indexOf('[');
        int tagClose = line.indexOf(']');

        if (tagOpen > 0 && tagClose > tagOpen)
        {
            server.sendContent_P(PSTR("<tr><td class='mono' style='white-space:nowrap'>"));
            server.sendContent(line.substring(0, tagOpen - 1).c_str());
            server.sendContent_P(PSTR("</td><td><span class='tag tag-info'>"));
            server.sendContent(line.substring(tagOpen + 1, tagClose).c_str());
            server.sendContent_P(PSTR("</span></td><td class='mono'>"));
            server.sendContent(line.substring(tagClose + 2).c_str());
            server.sendContent_P(PSTR("</td></tr>\n"));
        }
        else
        {
            server.sendContent_P(PSTR("<tr><td colspan='3' class='mono'>"));
            server.sendContent(line.c_str());
            server.sendContent_P(PSTR("</td></tr>\n"));
        }
    }

    server.sendContent_P(PSTR("</table>"));
    server.sendContent_P(LOG_POLL_JS);
    sendPageFoot();
}

// ── /log/entries ──────────────────────────────────────────────────────────────

// Append a JSON-escaped version of 's' into buf[cap], advancing pos.
// Stops writing (but keeps pos accurate) if buffer is nearly full.
static void jsonAppendEscaped(char* buf, size_t cap, size_t& pos, const String& s)
{
    for (unsigned i = 0; i < s.length(); ++i) {
        // Reserve space for longest escape sequence (2 chars) + null terminator
        if (pos + 3 >= cap) return;
        char c = s[i];
        if      (c == '"')  { buf[pos++] = '\\'; buf[pos++] = '"';  }
        else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
        else if (c == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n';  }
        else if (c == '\r') { buf[pos++] = '\\'; buf[pos++] = 'r';  }
        else if (c == '\t') { buf[pos++] = '\\'; buf[pos++] = 't';  }
        else                  buf[pos++] = c;
    }
}

// GET /log/entries?since=N
// Returns JSON: {"seq":N,"entries":[{"seq":N,"ts":"...","tag":"...","msg":"..."},...]}
// Entries with seq > since, ordered oldest-first so the browser can insertRow(1) each
// one and the newest naturally lands at the top of the table.
static void handle_log_entries()
{
    if (!is_authenticated()) return;

    uint32_t since = server.hasArg("since")
                     ? (uint32_t)server.arg("since").toInt()
                     : 0;

    const auto& history = getLogHistory();

    // Static buffer avoids heap allocation on every 3-second poll.
    static char jsonBuf[2048];
    size_t pos = 0;

    pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
                    "{\"seq\":%u,\"entries\":[", getLogSeq());

    bool first = true;
    for (const auto& entry : history) {
        if (entry.seq <= since) continue;
        // Stop if buffer is too full to safely write another entry
        if (pos + 64 >= sizeof(jsonBuf)) break;

        const String& line = entry.line;
        int tagOpen  = line.indexOf('[');
        int tagClose = line.indexOf(']');

        if (!first) jsonBuf[pos++] = ',';
        first = false;

        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
                        "{\"seq\":%u,\"ts\":\"", entry.seq);
        if (tagOpen > 1)
            jsonAppendEscaped(jsonBuf, sizeof(jsonBuf), pos,
                              line.substring(0, tagOpen - 1));
        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "\",\"tag\":\"");
        if (tagOpen >= 0 && tagClose > tagOpen)
            jsonAppendEscaped(jsonBuf, sizeof(jsonBuf), pos,
                              line.substring(tagOpen + 1, tagClose));
        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "\",\"msg\":\"");
        if (tagClose >= 0)
            jsonAppendEscaped(jsonBuf, sizeof(jsonBuf), pos,
                              line.substring(tagClose + 2));
        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "\"}");
    }

    if (pos + 3 < sizeof(jsonBuf)) {
        jsonBuf[pos++] = ']';
        jsonBuf[pos++] = '}';
        jsonBuf[pos]   = '\0';
    }

    server.send(200, "application/json", jsonBuf);
}

// ── /edit ─────────────────────────────────────────────────────────────────────

static const char* FILENAME = "/config.txt";

void handle_file_edit()
{
    if (!is_authenticated()) return;

    LittleFS.begin(true);

    if (server.method() == HTTP_GET)
    {
        File file = LittleFS.open(FILENAME, "r");
        if (!file) { server.send(500, "text/plain", "Failed to open file"); return; }

        sendPageHead("Config editor");
        sendPageNav("/edit");
        server.sendContent_P(PSTR("<h2>Config editor <small style='font-weight:400;color:#94a3b8'>"
                                   "/config.txt</small></h2>"
                                   "<form method='POST' action='/edit'>"
                                   "<textarea name='content' rows='22' spellcheck='false'>"));
        char buf[256];
        while (file.available())
        {
            int n = file.readBytes(buf, sizeof(buf));
            if (n > 0) server.sendContent(buf, n);
        }
        file.close();
        server.sendContent_P(PSTR("</textarea>"
                                   "<div class='actions'>"
                                   "<button class='btn btn-primary' type='submit'>&#128190; Save &amp; reload</button>"
                                   "<span style='color:#64748b;font-size:.85rem'>Changes take effect immediately.</span>"
                                   "</div></form>"));
        sendPageFoot();
        return;
    }

    if (server.method() == HTTP_POST)
    {
        if (!server.hasArg("content")) { server.send(400, "text/plain", "Bad Request"); return; }

        File file = LittleFS.open(FILENAME, "w");
        if (!file) { server.send(500, "text/plain", "Failed to open file for writing"); return; }
        file.print(server.arg("content"));
        file.close();

        DataStore::getInstance().load_from_file(FILENAME);
        invalidate_auth_cache();
        logPrintf("WEB", "config saved and reloaded via /edit");

        sendPageHead("Config editor");
        sendPageNav("/edit");
        server.sendContent_P(PSTR("<h2>Config editor</h2><div class='card'>"
                                   "<p style='color:#15803d;font-weight:500'>&#10003; Saved and reloaded.</p>"
                                   "<div class='actions'>"
                                   "<a class='btn btn-primary' href='/edit'>&#8592; Back to editor</a>"
                                   "</div></div>"));
        sendPageFoot();
        return;
    }

    server.send(405, "text/plain", "Method Not Allowed");
}

// ── /reboot ───────────────────────────────────────────────────────────────────

void handle_reboot()
{
    if (!is_authenticated()) return;

    sendPageHead("Rebooting");
    sendPageNav("/");
    server.sendContent_P(PSTR("<h2>Rebooting&hellip;</h2><div class='card'>"
                               "<p>The device is restarting. "
                               "<a href='/status'>Reload status</a> in a few seconds.</p>"
                               "</div>"));
    sendPageFoot();

    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP.restart();
}

// ── Server task / routing ─────────────────────────────────────────────────────

void web_server_task(void* pvParameters)
{
    server.on("/",          handle_home);
    server.on("/style.css", HTTP_GET, handle_style_css);
    server.on("/status",    HTTP_GET, handle_status);
    server.on("/edit",               handle_file_edit);
    server.on("/reboot",  HTTP_POST, handle_reboot);
    server.on("/log",         HTTP_GET,  handle_log);
    server.on("/log/entries", HTTP_GET,  handle_log_entries);
    server.on("/actions",            handle_actions);
    server.on("/messages",           handle_messages);
    server.on("/update",  HTTP_GET,  handle_update_get);
    server.on("/update",  HTTP_POST, handle_update_post, handle_update_upload);
    server.begin();

    while (true)
    {
        server.handleClient();
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
}
