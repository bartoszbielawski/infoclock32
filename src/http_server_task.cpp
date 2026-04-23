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
#include <task_registry.hpp>

// Handlers defined in other translation units
void handle_push();
void handle_actions();
void handle_messages();
void handle_update_get();
void handle_update_post();
void handle_update_upload();

// ── / (home dashboard) ────────────────────────────────────────────────────────

void handle_home()
{
    char uptime[32], heap[24], rssiStr[20];
    getStatusFields(uptime, sizeof(uptime), heap, sizeof(heap), rssiStr, sizeof(rssiStr));

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

    sendPageHead("Home");
    sendPageNav("/");

    server.sendContent_P(PSTR("<h2>&#9881;&#65039; Device status</h2>"
                               "<table style='margin-bottom:24px'>"
                               "<tr><th colspan='2'>&#127760; Network</th></tr>"));
    sendRow("IP address", WiFi.localIP().toString().c_str());
    sendRow("Hostname",   WiFi.getHostname());
    sendRow("SSID",       WiFi.SSID().c_str());
    server.sendContent_P(PSTR("<tr><td class='label'>Signal</td><td id='rssi'>"));
    server.sendContent(rssiStr);
    server.sendContent_P(PSTR("</td></tr>\n<tr><th colspan='2'>&#128421;&#65039; System</th></tr>"));
    sendRow("Firmware",   APP_VERSION " &bull; built " BUILD_DATE);
    server.sendContent_P(PSTR("<tr><td class='label'>Uptime</td><td id='uptime'>"));
    server.sendContent(uptime);
    server.sendContent_P(PSTR("</td></tr>\n<tr><td class='label'>Free heap</td><td id='heap'>"));
    server.sendContent(heap);
    server.sendContent_P(PSTR("</td></tr>\n"));
    sendRow("Chip",       ESP.getChipModel());
    server.sendContent_P(PSTR("<tr><th colspan='2'>&#128225; MQTT</th></tr>"
                               "<tr><td class='label'>Status</td><td id='mqtt'>"));
    server.sendContent(mqttStatus);
    server.sendContent_P(PSTR("</td></tr>\n"));
    server.sendContent_P(PSTR("</table>"
                               "<h2>&#9889;&#65039; Actions</h2>"
                               "<div class='card'>"
                               "<h3 class='card-title'>&#128172; Push message</h3>"
                               "<form method='POST' action='/actions'>"
                               "<input type='hidden' name='action' value='push'>"
                               "<input name='message' type='text' placeholder='Message to scroll&hellip;' "
                               "class='form-input'>"
                               "<button class='btn btn-primary' type='submit'>&#9654; Send</button>"
                               "</form></div>"
                               "<div class='card'>"
                               "<h3 class='card-title'>&#9728;&#65039; Brightness (0&ndash;15)</h3>"
                               "<form method='POST' action='/actions'>"
                               "<input type='hidden' name='action' value='brightness'>"
                               "<div class='flex-between'>"
                               "<input name='level' type='range' min='0' max='15' value='"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("' oninput='this.nextElementSibling.textContent=this.value'>"
                               "<span>"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("</span></div>"
                               "<button class='btn btn-primary' type='submit'>Set</button>"
                               "</form></div>"
                               "<div class='card'>"
                               "<h3 class='card-title'>"
                               "&#128260; Reboot</h3>"
                               "<form method='POST' action='/reboot'>"
                               "<button class='btn btn-danger' type='submit'>Reboot device</button>"
                               "</form></div>"
                               "<script>"
                               "function startPolling(e,t,o){let l=t,n=async function(){try{let s=await fetch(e);if(!s.ok)throw new Error(s.status);o(await s.json()),l=t}catch(e){l=Math.min(1.5*l,3e4)}setTimeout(n,l)};n()}"
                               "startPolling('/api/status',2000,function(d){"
                               "document.getElementById('uptime').textContent=d.uptime;"
                               "document.getElementById('heap').textContent=d.heap;"
                               "document.getElementById('rssi').textContent=d.rssi;"
                               "});"
                               "</script>"));
    sendPageFoot();
}

// ── Task list helper ──────────────────────────────────────────────────────────

static const char* taskStateName(eTaskState s)
{
    switch (s) {
        case eRunning:   return "Run";
        case eReady:     return "Ready";
        case eBlocked:   return "Blocked";
        case eSuspended: return "Suspended";
        default:         return "Dead";
    }
}

static const char* taskStateColor(eTaskState s)
{
    switch (s) {
        case eRunning:   return "#15803d";
        case eReady:     return "#1d4ed8";
        case eBlocked:   return "#64748b";
        case eSuspended: return "#b45309";
        default:         return "#dc2626";
    }
}

static void sendTasksSection()
{
    const TaskRegistry& reg = TaskRegistry::getInstance();
    int n = reg.count();
    if (n == 0) return;

    server.sendContent_P(PSTR("<tr><th colspan='2'>&#129529; Tasks</th></tr>"));

    char buf[256];
    for (int i = 0; i < n; ++i) {
        const RegisteredTask& t = reg.get(i);
        eTaskState state = eTaskGetState(t.handle);
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(t.handle);

        snprintf(buf, sizeof(buf),
            "<tr><td class='label mono'>%s</td>"
            "<td><span style='color:%s;font-weight:500'>%s</span>"
            " &bull; stack&nbsp;free&nbsp;%u&nbsp;B</td></tr>\n",
            t.name,
            taskStateColor(state),
            taskStateName(state),
            (unsigned)watermark * sizeof(StackType_t));
        server.sendContent(buf);
    }
}

// ── /status ───────────────────────────────────────────────────────────────────

void handle_status()
{
    char uptime[32], heap[24], rssiStr[20];
    getStatusFields(uptime, sizeof(uptime), heap, sizeof(heap), rssiStr, sizeof(rssiStr));

    sendPageHead("Status");
    sendPageNav("/status");
    server.sendContent_P(PSTR("<h2>Device status</h2><table>"
                               "<tr><th colspan='2'>&#127760; Network</th></tr>"));
    sendRow("IP address",  WiFi.localIP().toString().c_str());
    sendRow("Hostname",    WiFi.getHostname());
    sendRow("SSID",        WiFi.SSID().c_str());
    server.sendContent_P(PSTR("<tr><td class='label'>Signal</td><td id='rssi'>"));
    server.sendContent(rssiStr);
    server.sendContent_P(PSTR("</td></tr>\n"));
    sendRow("MAC address", WiFi.macAddress().c_str());
    server.sendContent_P(PSTR("<tr><th colspan='2'>&#9881;&#65039; System</th></tr>"));
    sendRow("Firmware",    APP_VERSION " &bull; built " BUILD_DATE " " BUILD_TIME);
    server.sendContent_P(PSTR("<tr><td class='label'>Uptime</td><td id='uptime'>"));
    server.sendContent(uptime);
    server.sendContent_P(PSTR("</td></tr>\n<tr><td class='label'>Free heap</td><td id='heap'>"));
    server.sendContent(heap);
    server.sendContent_P(PSTR("</td></tr>\n"));
    sendRow("Chip",        ESP.getChipModel());

    auto rtSnap = RuntimeStore::getInstance().snapshot();
    if (!rtSnap.empty())
    {
        server.sendContent_P(PSTR("<tr><th colspan='2'>&#9889;&#65039; Runtime values</th></tr>"));
        for (const auto& kv : rtSnap) {
            server.sendContent_P(PSTR("<tr><td class='label'>"));
            server.sendContent(kv.first.c_str());
            server.sendContent_P(PSTR("</td><td id='rt-"));
            server.sendContent(kv.first.c_str());
            server.sendContent_P(PSTR("'>"));
            server.sendContent(kv.second.c_str());
            server.sendContent_P(PSTR("</td></tr>\n"));
        }
    }

    sendTasksSection();

    server.sendContent_P(PSTR("</table>"
                               "<script>"
                               "function startPolling(e,t,o){let l=t,n=async function(){try{let s=await fetch(e);if(!s.ok)throw new Error(s.status);o(await s.json()),l=t}catch(e){l=Math.min(1.5*l,3e4)}setTimeout(n,l)};n()}"
                               "startPolling('/api/status',2000,function(d){"
                               "document.getElementById('uptime').textContent=d.uptime;"
                               "document.getElementById('heap').textContent=d.heap;"
                               "document.getElementById('rssi').textContent=d.rssi;"
                               "});"
                               "startPolling('/api/runtime',2000,function(d){"
                               "for(let k in d.entries){let e=document.getElementById('rt-'+k);e&&(e.textContent=d.entries[k])}"
                               "});"
                               "</script>"));
    sendPageFoot();
}

// ── /log ──────────────────────────────────────────────────────────────────────

void handle_log()
{
    if (!is_authenticated()) return;

    char seqAttr[32];
    snprintf(seqAttr, sizeof(seqAttr), "%u", getLogSeq());

    sendPageHead("Log", "");
    sendPageNav("/log");
    server.sendContent_P(PSTR("<h2>Log <small style='font-weight:400;color:#94a3b8'>"
                               "(newest first, last 40 entries, live)</small></h2>"
                               "<table><tr><th>Timestamp</th><th>Tag</th><th>Message</th></tr>"
                               "<tbody id='log-body' data-seq='"));
    server.sendContent(seqAttr);
    server.sendContent_P(PSTR("'>\n"));

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

    server.sendContent_P(PSTR("</tbody></table>"
        "<script>"
        "setInterval(function(){"
          "var tb=document.getElementById('log-body');"
          "if(!tb)return;"
          "var since=tb.dataset.seq||0;"
          "fetch('/log/entries?since='+since,{credentials:'include'})"
          ".then(function(r){return r.ok?r.json():null;})"
          ".then(function(d){"
            "if(!d)return;"
            "if(d.seq)tb.dataset.seq=d.seq;"
            "if(!d.entries||!d.entries.length)return;"
            "d.entries.forEach(function(e){"
              "var tr=document.createElement('tr');"
              "tr.innerHTML=\"<td class='mono' style='white-space:nowrap'>\"+e.ts+"
                "\"</td><td><span class='tag tag-info'>\"+e.tag+"
                "\"</span></td><td class='mono'>\"+e.msg+\"</td>\";"
              "tb.insertBefore(tr,tb.firstChild);"
            "});"
            "while(tb.rows.length>40)tb.lastElementChild.remove();"
          "});"
        "},5000);"
        "</script>"));
    sendPageFoot();
}

// ── /log/entries ──────────────────────────────────────────────────────────────

// Append a JSON-escaped version of 's' into buf[cap], advancing pos.
// Stops writing (but keeps pos accurate) if buffer is nearly full.
static void jsonAppendEscaped(char* buf, size_t cap, size_t& pos, const String& s)
{
    for (unsigned i = 0; i < s.length(); ++i) {
        // Reserve space for longest escape sequence (\uXXXX = 6 chars) + null terminator
        if (pos + 7 >= cap) return;
        char c = s[i];
        if      (c == '"')  { buf[pos++] = '\\'; buf[pos++] = '"';  }
        else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
        else if (c == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n';  }
        else if (c == '\r') { buf[pos++] = '\\'; buf[pos++] = 'r';  }
        else if (c == '\t') { buf[pos++] = '\\'; buf[pos++] = 't';  }
        else if ((unsigned char)c < 0x20) {
            pos += snprintf(buf + pos, cap - pos, "\\u%04x", (unsigned char)c);
        }
        else                  buf[pos++] = c;
    }
}

// GET /log/entries[?since=<seq>] — returns JSON {"seq":N,"entries":[{"ts":"...","tag":"...","msg":"..."},...]}
// Newest entry first (matches the initial HTML render order).
// When ?since=N is given, only entries with seq > N are returned (delta polling).
// Streams one entry at a time so there is no fixed buffer size limit.
static void handle_log_entries()
{
    if (!is_authenticated()) return;

    uint32_t since = 0;
    if (server.hasArg("since"))
        since = (uint32_t)server.arg("since").toInt();

    // Copy relevant entries under mutex so the HTTP task sees updates from other cores.
    std::vector<LogEntry> entries;
    uint32_t currentSeq = copyLogEntriesSince(since, entries);

    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    char seqBuf[32];
    snprintf(seqBuf, sizeof(seqBuf), "{\"seq\":%u,\"entries\":[", currentSeq);
    server.sendContent(seqBuf);

    char entryBuf[600]; // sized for worst-case single entry (see comment in jsonAppendEscaped)
    bool first = true;

    for (const auto& entry : entries) {
        const String& line = entry.line;
        int tagOpen  = line.indexOf('[');
        int tagClose = line.indexOf(']');

        size_t pos = 0;
        if (!first) entryBuf[pos++] = ',';
        first = false;

        pos += snprintf(entryBuf + pos, sizeof(entryBuf) - pos, "{\"ts\":\"");
        if (tagOpen > 1)
            jsonAppendEscaped(entryBuf, sizeof(entryBuf), pos,
                              line.substring(0, tagOpen - 1));
        pos += snprintf(entryBuf + pos, sizeof(entryBuf) - pos, "\",\"tag\":\"");
        if (tagOpen >= 0 && tagClose > tagOpen)
            jsonAppendEscaped(entryBuf, sizeof(entryBuf), pos,
                              line.substring(tagOpen + 1, tagClose));
        pos += snprintf(entryBuf + pos, sizeof(entryBuf) - pos, "\",\"msg\":\"");
        if (tagClose >= 0)
            jsonAppendEscaped(entryBuf, sizeof(entryBuf), pos,
                              line.substring(tagClose + 2));
        pos += snprintf(entryBuf + pos, sizeof(entryBuf) - pos, "\"}");

        server.sendContent(entryBuf);
    }

    server.sendContent("]}");
}

// ── /edit ─────────────────────────────────────────────────────────────────────

static const char* FILENAME = "/config.txt";

void handle_file_edit()
{
    if (!is_authenticated()) return;

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

// ── /api/status ───────────────────────────────────────────────────────────────

void handle_api_status()
{
    if (!is_authenticated()) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    char uptime[32], heap[24], rssi[20];
    getStatusFields(uptime, sizeof(uptime), heap, sizeof(heap), rssi, sizeof(rssi));

    std::string mqttServer = DataStore::getInstance().get_value("mqtt_server", "");
    bool mqttConnected = !mqttServer.empty() && mqtt_is_connected();

    char json[512];
    snprintf(json, sizeof(json),
        "{\"uptime\":\"%s\",\"heap\":\"%s\",\"rssi\":\"%s\","
        "\"ip\":\"%s\",\"hostname\":\"%s\",\"ssid\":\"%s\","
        "\"mqtt\":%s,\"firmware\":\"" APP_VERSION "\",\"chip\":\"%s\"}",
        uptime, heap, rssi,
        WiFi.localIP().toString().c_str(),
        WiFi.getHostname(),
        WiFi.SSID().c_str(),
        mqttConnected ? "true" : "false",
        ESP.getChipModel());

    server.send(200, "application/json", json);
}

// ── /api/runtime ──────────────────────────────────────────────────────────────

void handle_api_runtime()
{
    if (!is_authenticated()) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    auto rtSnap = RuntimeStore::getInstance().snapshot();

    char json[1024];
    size_t pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"entries\":{");

    bool first = true;
    for (const auto& kv : rtSnap) {
        if (sizeof(json) - pos < 300) break;
        pos += snprintf(json + pos, sizeof(json) - pos,
                        "%s\"%s\":\"%s\"",
                        first ? "" : ",",
                        kv.first.c_str(), kv.second.c_str());
        first = false;
    }
    snprintf(json + pos, sizeof(json) - pos, "}}");

    server.send(200, "application/json", json);
}

// ── Server task / routing ─────────────────────────────────────────────────────

void web_server_task(void* pvParameters)
{
    registerTask("WebServer");
    server.on("/push",               handle_push);
    server.on("/",          handle_home);
    server.on("/style.css", HTTP_GET, handle_style_css);
    server.on("/status",    HTTP_GET, handle_status);
    server.on("/edit",               handle_file_edit);
    server.on("/reboot",  HTTP_POST, handle_reboot);
    server.on("/log",         HTTP_GET,  handle_log);
    server.on("/log/entries", HTTP_GET,  handle_log_entries);
    server.on("/api/status",  HTTP_GET, handle_api_status);
    server.on("/api/runtime", HTTP_GET, handle_api_runtime);
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
