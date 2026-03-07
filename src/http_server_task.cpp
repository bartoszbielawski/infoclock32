#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <Update.h>
#include <vector>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <logger.hpp>
#include <timezone_utils.hpp>
#include <version.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <mqtt_task.h>

// Web server instance
WebServer server(80);

WebServer* getWebServer() { return &server; }

// ── shared CSS / layout helpers ───────────────────────────────────────────────

static const char CSS[] PROGMEM = R"css(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
     background:#f0f4f8;color:#1e293b;min-height:100vh}
header{background:#1d4ed8;color:#fff;padding:14px 24px;
       display:flex;align-items:center;gap:16px;box-shadow:0 2px 6px #0003}
header h1{font-size:1.2rem;font-weight:600;letter-spacing:.5px}
header span{font-size:.8rem;opacity:.7}
nav{background:#fff;border-bottom:1px solid #e2e8f0;padding:0 24px;
    display:flex;gap:4px}
nav a{display:inline-block;padding:10px 14px;text-decoration:none;
      color:#475569;font-size:.9rem;border-bottom:3px solid transparent}
nav a:hover{color:#1d4ed8;border-bottom-color:#93c5fd}
nav a.active{color:#1d4ed8;border-bottom-color:#1d4ed8;font-weight:600}
main{max-width:860px;margin:32px auto;padding:0 20px}
h2{font-size:1.1rem;font-weight:600;color:#1e293b;margin-bottom:16px;
   padding-bottom:8px;border-bottom:2px solid #e2e8f0}
table{width:100%;border-collapse:collapse;background:#fff;
      border-radius:8px;overflow:hidden;box-shadow:0 1px 4px #0001}
th{background:#1d4ed8;color:#fff;text-align:left;padding:10px 14px;
   font-size:.85rem;font-weight:600;letter-spacing:.3px}
td{padding:10px 14px;border-bottom:1px solid #e2e8f0;font-size:.9rem}
tr:last-child td{border-bottom:none}
tr:nth-child(even) td{background:#f8fafc}
td.label{color:#64748b;font-weight:500;width:160px}
td.mono{font-family:monospace;font-size:.82rem}
textarea{width:100%;font-family:monospace;font-size:.85rem;padding:10px;
         border:1px solid #cbd5e1;border-radius:6px;resize:vertical;
         background:#fff;color:#1e293b}
.btn{display:inline-block;padding:9px 20px;border:none;border-radius:6px;
     font-size:.9rem;font-weight:500;cursor:pointer;text-decoration:none}
.btn-primary{background:#1d4ed8;color:#fff}
.btn-primary:hover{background:#1e40af}
.btn-danger{background:#dc2626;color:#fff}
.btn-danger:hover{background:#b91c1c}
.actions{margin-top:16px;display:flex;gap:10px;align-items:center}
.card{background:#fff;border-radius:8px;box-shadow:0 1px 4px #0001;
      padding:20px;margin-bottom:20px}
.tag{display:inline-block;padding:2px 8px;border-radius:4px;
     font-size:.75rem;font-weight:600;font-family:monospace}
.tag-info{background:#dbeafe;color:#1d4ed8}
.tag-warn{background:#fef3c7;color:#92400e}
.tag-err {background:#fee2e2;color:#991b1b}
)css";

static String pageHead(const char* title, const char* extraHead = "")
{
    String h = F("<!DOCTYPE html><html lang='en'><head>"
                 "<meta charset='utf-8'>"
                 "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<title>");
    h += title;
    h += F(" \xe2\x80\x94 ");
    h += WiFi.getHostname();
    h += F("</title><style>");
    h += CSS;
    h += F("</style>");
    h += extraHead;
    h += F("</head><body>"
           "<header><h1>&#128336; ");
    h += WiFi.getHostname();
    h += F("</h1><span>");
    h += WiFi.localIP().toString();
    h += F("</span></header>");
    return h;
}

static String pageNav(const char* active)
{
    auto link = [&](const char* href, const char* label) -> String {
        String cls = (strcmp(href, active) == 0) ? " class='active'" : "";
        return String(F("<a href='")) + href + F("'") + cls + F(">") + label + F("</a>");
    };
    return String(F("<nav>"))
        + link("/",        "Home")
        + link("/status",  "Status")
        + link("/log",     "Log")
        + link("/edit",    "Config")
        + link("/actions",  "Actions")
        + link("/messages", "Messages")
        + link("/update",   "Update")
        + F("</nav><main>");
}

static const char PAGE_FOOT[] PROGMEM =
    "</main>"
    "<footer style='text-align:center;padding:10px 0 14px;"
    "font-size:.75rem;color:#94a3b8'>"
    APP_VERSION " &bull; built " BUILD_DATE " " BUILD_TIME
    "</footer>"
    "</body></html>";

// ── auth ──────────────────────────────────────────────────────────────────────

static bool check_auth_header()
{
    if (!server.hasHeader("Authorization")) return false;
    String authHeader = server.header("Authorization");
    if (!authHeader.startsWith("Basic ")) return false;

    String encoded = authHeader.substring(6);
    unsigned char decoded[64] = {};
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                              (const unsigned char*)encoded.c_str(), encoded.length()) != 0)
        return false;
    decoded[decoded_len] = '\0';

    String credentials = String((char*)decoded);
    int colon = credentials.indexOf(':');
    if (colon == -1) return false;

    return (credentials.substring(0, colon)  == "admin" &&
            credentials.substring(colon + 1) == "password");
}

bool is_authenticated()
{
    if (check_auth_header()) return true;
    server.sendHeader("WWW-Authenticate", "Basic realm=\"infoclock32\"");
    server.send(401, "text/plain", "Unauthorized");
    return false;
}

// ── handlers ──────────────────────────────────────────────────────────────────

void handle_home();
void handle_file_edit();
void handle_reboot();
void handle_log();
void handle_status();
void handle_actions();
void handle_update_get();
void handle_update_post();
void handle_update_upload();
void handle_messages();

void web_server_task(void* pvParameters)
{
    server.on("/", handle_home);
    server.on("/status",  HTTP_GET,  handle_status);
    server.on("/edit",               handle_file_edit);
    server.on("/reboot",  HTTP_POST, handle_reboot);
    server.on("/log",     HTTP_GET,  handle_log);
    server.on("/actions",            handle_actions);
    server.on("/messages",            handle_messages);
    server.on("/update",  HTTP_GET,  handle_update_get);
    server.on("/update",  HTTP_POST, handle_update_post, handle_update_upload);
    server.begin();

    while (true)
    {
        server.handleClient();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ── / (home dashboard) ────────────────────────────────────────────────────────

void handle_home()
{
    // ── status data ───────────────────────────────────────────────────────────
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
    const char* quality = rssi >= -60 ? "excellent" : rssi >= -70 ? "good" : rssi >= -80 ? "fair" : "weak";
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm (%s)", rssi, quality);

    // MQTT status
    auto& ds = DataStore::getInstance();
    std::string mqttServer = ds.get_value("mqtt_server", "");
    String mqttStatus;
    if (mqttServer.empty())
        mqttStatus = F("<span style='color:#94a3b8'>not configured</span>");
    else if (mqtt_is_connected())
        mqttStatus = String(F("&#10003; connected to ")) + mqttServer.c_str();
    else
        mqttStatus = String(F("&#9888; disconnected (")) + mqttServer.c_str() + F(")");

    int curBrightness = atoi(ds.get_value("brightness", "7").c_str());

    auto row = [](const char* label, const String& value) -> String {
        return String(F("<tr><td class='label'>")) + label +
               F("</td><td>") + value + F("</td></tr>\n");
    };

    // ── page ──────────────────────────────────────────────────────────────────
    String html = pageHead("Home", "<meta http-equiv='refresh' content='30'>");
    html += pageNav("/");

    // Status table
    html += F("<h2>&#9881;&#65039; Device status</h2>"
              "<table style='margin-bottom:24px'>"
              "<tr><th colspan='2'>&#127760; Network</th></tr>");
    html += row("IP address",  WiFi.localIP().toString());
    html += row("Hostname",    WiFi.getHostname());
    html += row("SSID",        WiFi.SSID());
    html += row("Signal",      rssiStr);
    html += F("<tr><th colspan='2'>&#128421;&#65039; System</th></tr>");
    html += row("Firmware",  F(APP_VERSION " &bull; built " BUILD_DATE));
    html += row("Uptime",    uptime);
    html += row("Free heap", heap);
    html += row("Chip",      ESP.getChipModel());
    html += F("<tr><th colspan='2'>&#128225; MQTT</th></tr>");
    html += row("Status", mqttStatus);
    html += F("</table>");

    // Action cards — forms POST to /actions
    html += F("<h2>&#9889;&#65039; Actions</h2>");

    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#128172; Push message</h3>"
              "<form method='POST' action='/actions'>"
              "<input type='hidden' name='action' value='push'>"
              "<input name='message' type='text' placeholder='Message to scroll&hellip;' "
              "style='width:100%;padding:8px 10px;border:1px solid #cbd5e1;border-radius:6px;"
              "font-size:.9rem;margin-bottom:10px;background:#fff;color:#1e293b'>"
              "<button class='btn btn-primary' type='submit'>&#9654; Send</button>"
              "</form></div>");

    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#9728;&#65039; Brightness (0&ndash;15)</h3>"
              "<form method='POST' action='/actions'>"
              "<input type='hidden' name='action' value='brightness'>"
              "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
              "<input name='level' type='range' min='0' max='15' value='");
    html += curBrightness;
    html += F("' style='flex:1' oninput='this.nextElementSibling.textContent=this.value'>"
              "<span style='font-family:monospace;min-width:2ch'>");
    html += curBrightness;
    html += F("</span></div>"
              "<button class='btn btn-primary' type='submit'>Set</button>"
              "</form></div>");

    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#128260; Reboot</h3>"
              "<form method='POST' action='/reboot'>"
              "<button class='btn btn-danger' type='submit'>Reboot device</button>"
              "</form></div>");

    html += PAGE_FOOT;
    server.send(200, "text/html", html);
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
    const char* quality = rssi >= -60 ? "excellent" : rssi >= -70 ? "good" : rssi >= -80 ? "fair" : "weak";
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm (%s)", rssi, quality);

    auto row = [](const char* label, const String& value) -> String {
        return String(F("<tr><td class='label'>")) + label +
               F("</td><td>") + value + F("</td></tr>\n");
    };

    String html = pageHead("Status", "<meta http-equiv='refresh' content='10'>");
    html += pageNav("/status");
    html += F("<h2>Device status</h2><table>"
              "<tr><th colspan='2'>&#127760; Network</th></tr>");
    html += row("IP address",  WiFi.localIP().toString());
    html += row("Hostname",    WiFi.getHostname());
    html += row("SSID",        WiFi.SSID());
    html += row("Signal",      rssiStr);
    html += row("MAC address", WiFi.macAddress());
    html += F("<tr><th colspan='2'>&#9881;&#65039; System</th></tr>");
    html += row("Firmware",    F(APP_VERSION " &bull; built " BUILD_DATE " " BUILD_TIME));
    html += row("Uptime",      uptime);
    html += row("Free heap",   heap);
    html += row("Chip",        ESP.getChipModel());

    // Runtime values — shown only when at least one task has published something
    auto rtSnap = RuntimeStore::getInstance().snapshot();
    if (!rtSnap.empty())
    {
        html += F("<tr><th colspan='2'>&#9889;&#65039; Runtime values</th></tr>");
        for (const auto& kv : rtSnap)
            html += row(kv.first.c_str(), String(kv.second.c_str()));
    }

    html += F("</table>");
    html += PAGE_FOOT;
    server.send(200, "text/html", html);
}

// ── /log ──────────────────────────────────────────────────────────────────────

void handle_log()
{
    if (!is_authenticated()) return;

    String html = pageHead("Log", "<meta http-equiv='refresh' content='5'>");
    html += pageNav("/log");
    html += F("<h2>Log <small style='font-weight:400;color:#94a3b8'>"
              "(newest first, last 40 entries, auto-refreshes)</small></h2>"
              "<table><tr><th>Timestamp</th><th>Tag</th><th>Message</th></tr>\n");

    const auto &history = getLogHistory();
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        // Format: "YYYY-MM-DDTHH:MM:SS [TAG] message"
        const String& line = *it;
        int tagOpen  = line.indexOf('[');
        int tagClose = line.indexOf(']');

        if (tagOpen > 0 && tagClose > tagOpen)
        {
            String ts  = line.substring(0, tagOpen - 1);
            String tag = line.substring(tagOpen + 1, tagClose);
            String msg = line.substring(tagClose + 2);
            html += String(F("<tr><td class='mono' style='white-space:nowrap'>")) + ts + F("</td>"
                      "<td><span class='tag tag-info'>") + tag + F("</span></td>"
                      "<td class='mono'>") + msg + F("</td></tr>\n");
        }
        else
        {
            html += String(F("<tr><td colspan='3' class='mono'>")) + line + F("</td></tr>\n");
        }
    }

    html += F("</table>");
    html += PAGE_FOOT;
    server.send(200, "text/html", html);
}

// ── /edit ─────────────────────────────────────────────────────────────────────

const char* FILENAME = "/config.txt";

void handle_file_edit()
{
    if (!is_authenticated()) return;

    LittleFS.begin(true);

    if (server.method() == HTTP_GET)
    {
        File file = LittleFS.open(FILENAME, "r");
        if (!file) { server.send(500, "text/plain", "Failed to open file"); return; }

        String fileContent;
        while (file.available()) fileContent += (char)file.read();
        file.close();

        String html = pageHead("Config editor");
        html += pageNav("/edit");
        html += F("<h2>Config editor <small style='font-weight:400;color:#94a3b8'>"
                  "/config.txt</small></h2>"
                  "<form method='POST' action='/edit'>"
                  "<textarea name='content' rows='22' spellcheck='false'>");
        html += fileContent;
        html += F("</textarea>"
                  "<div class='actions'>"
                  "<button class='btn btn-primary' type='submit'>&#128190; Save &amp; reload</button>"
                  "<span style='color:#64748b;font-size:.85rem'>Changes take effect immediately.</span>"
                  "</div></form>");
        html += PAGE_FOOT;
        server.send(200, "text/html", html);
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
        logPrintf("WEB", "config saved and reloaded via /edit");

        String html = pageHead("Config editor");
        html += pageNav("/edit");
        html += F("<h2>Config editor</h2><div class='card'>"
                  "<p style='color:#15803d;font-weight:500'>&#10003; Saved and reloaded.</p>"
                  "<div class='actions'><a class='btn btn-primary' href='/edit'>&#8592; Back to editor</a></div>"
                  "</div>");
        html += PAGE_FOOT;
        server.send(200, "text/html", html);
        return;
    }

    server.send(405, "text/plain", "Method Not Allowed");
}

// ── /reboot ───────────────────────────────────────────────────────────────────

void handle_reboot()
{
    if (!is_authenticated()) return;

    String html = pageHead("Rebooting");
    html += pageNav("/");
    html += F("<h2>Rebooting&hellip;</h2><div class='card'>"
              "<p>The device is restarting. "
              "<a href='/status'>Reload status</a> in a few seconds.</p>"
              "</div>");
    html += PAGE_FOOT;
    server.send(200, "text/html", html);

    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP.restart();
}

// ── /actions ──────────────────────────────────────────────────────────────────

void handle_actions()
{
    if (!is_authenticated()) return;

    auto& rmd = ResourceManager<LMDS>::getInstance();
    String result;

    if (server.method() == HTTP_POST)
    {
        String action = server.arg("action");

        if (action == "push")
        {
            String msg = server.arg("message");
            if (!msg.isEmpty())
            {
                if (rmd.make_access_request())
                {
                    scrollMessage(std::string(msg.c_str()), rmd.getResourceRef(), 50);
                    rmd.release_access();
                    result = "&#10003; Message displayed.";
                    logPrintf("WEB", "push message via /actions");
                }
                else { result = "&#9888; Display busy &mdash; try again."; }
            }
        }
        else if (action == "brightness")
        {
            int level = server.arg("level").toInt();
            if (level >= 0 && level <= 15)
            {
                if (rmd.make_access_request())
                {
                    rmd.getResourceRef().setIntensity((uint8_t)level);
                    rmd.release_access();
                    DataStore::getInstance().set_value("brightness", std::to_string(level));
                    DataStore::getInstance().save_to_file("/config.txt");
                    result = "&#10003; Brightness set to " + String(level) + ".";
                    logPrintf("WEB", "brightness set to %d via /actions", level);
                }
                else { result = "&#9888; Display busy &mdash; try again."; }
            }
        }
        else if (action == "nightmode")
        {
            String start = server.arg("night_start");
            String end   = server.arg("night_end");
            int    nbr   = server.arg("night_brightness").toInt();
            nbr = max(0, min(15, nbr));
            auto& ds = DataStore::getInstance();
            ds.set_value("night_start",      start.c_str());
            ds.set_value("night_end",        end.c_str());
            ds.set_value("night_brightness", std::to_string(nbr));
            ds.save_to_file("/config.txt");
            result = "&#10003; Night mode saved.";
            logPrintf("WEB", "night mode %s-%s brightness %d via /actions",
                      start.c_str(), end.c_str(), nbr);
        }
        else if (action == "hostname")
        {
            String hn = server.arg("hostname");
            hn.trim();
            bool valid = !hn.isEmpty() && hn.length() <= 63;
            for (unsigned i = 0; valid && i < hn.length(); i++) {
                char c = hn[i];
                if (!isalnum((unsigned char)c) && c != '-') valid = false;
            }
            if (!hn.isEmpty() && (hn[0] == '-' || hn[hn.length()-1] == '-')) valid = false;
            if (valid) {
                DataStore::getInstance().set_value("hostname", hn.c_str());
                DataStore::getInstance().save_to_file("/config.txt");
                WiFi.setHostname(hn.c_str());
                result = "&#10003; Hostname set to &ldquo;" + hn + "&rdquo;. Reboot to apply fully.";
                logPrintf("WEB", "hostname set to %s via /actions", hn.c_str());
            } else {
                result = "&#9888;&#65039; Invalid hostname &mdash; use letters, digits and hyphens only (max 63 chars).";
            }
        }
        else if (action == "timezone")
        {
            String tz = server.arg("tz");
            tz.trim();
            if (!tz.isEmpty())
            {
                DataStore::getInstance().set_value("timezone", tz.c_str());
                DataStore::getInstance().save_to_file("/config.txt");
                apply_timezone();
                result = "&#10003; Timezone applied: " + tz;
                logPrintf("WEB", "timezone set to %s via /actions", tz.c_str());
            }
            else
            {
                result = "&#9888;&#65039; Timezone string must not be empty.";
            }
        }
    }

    int    curBrightness = atoi(DataStore::getInstance().get_value("brightness", "7").c_str());
    String curTz         = DataStore::getInstance().get_value("timezone", "UTC0").c_str();
    String curHostname   = WiFi.getHostname();   // reflects the live value

    String html = pageHead("Actions");
    html += pageNav("/actions");
    html += F("<h2>Actions</h2>");

    if (!result.isEmpty())
    {
        html += F("<div class='card' style='border-left:3px solid #15803d;margin-bottom:20px'>"
                  "<p style='color:#15803d;font-weight:500'>");
        html += result;
        html += F("</p></div>");
    }

    // Push message
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#128172; Push message</h3>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='push'>"
              "<input name='message' type='text' placeholder='Message to scroll&hellip;' "
              "style='width:100%;padding:8px 10px;border:1px solid #cbd5e1;border-radius:6px;"
              "font-size:.9rem;margin-bottom:10px;background:#fff;color:#1e293b'>"
              "<button class='btn btn-primary' type='submit'>&#9654; Send</button>"
              "</form></div>");

    // Brightness
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#9728;&#65039; Brightness (0&ndash;15)</h3>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='brightness'>"
              "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
              "<input name='level' type='range' min='0' max='15' value='");
    html += curBrightness;
    html += F("' style='flex:1' oninput='this.nextElementSibling.textContent=this.value'>"
              "<span style='font-family:monospace;min-width:2ch'>");
    html += curBrightness;
    html += F("</span></div>"
              "<button class='btn btn-primary' type='submit'>Set</button>"
              "</form></div>");

    // Night mode
    {
        auto& ds = DataStore::getInstance();
        String nStart = ds.get_value("night_start", "").c_str();
        String nEnd   = ds.get_value("night_end",   "").c_str();
        int    nBr    = ds.get_value("night_brightness", 1);

        html += F("<div class='card'>"
                  "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                  "&#127769; Night mode</h3>"
                  "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                  "Dims the display between two times. Leave blank to disable.</p>"
                  "<form method='POST'>"
                  "<input type='hidden' name='action' value='nightmode'>"
                  "<div style='display:flex;gap:16px;align-items:center;flex-wrap:wrap;margin-bottom:10px'>"
                  "<label style='font-size:.9rem'>From</label>"
                  "<input type='time' name='night_start' value='");
        html += nStart;
        html += F("' style='padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                  "<label style='font-size:.9rem'>To</label>"
                  "<input type='time' name='night_end' value='");
        html += nEnd;
        html += F("' style='padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                  "<label style='font-size:.9rem'>Brightness</label>"
                  "<input type='number' name='night_brightness' min='0' max='15' value='");
        html += nBr;
        html += F("' style='width:60px;padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                  "</div>"
                  "<button class='btn btn-primary' type='submit'>Save</button>"
                  "</form></div>");
    }

    // Timezone
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#127760; Timezone</h3>"
              "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
              "Pick a preset, or enter a POSIX TZ string directly. "
              "Applies immediately&nbsp;&mdash; no reboot needed.</p>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='timezone'>"
              "<select style='display:block;width:100%;padding:6px 8px;border:1px solid #cbd5e1;"
              "border-radius:6px;font-size:.9rem;margin-bottom:8px;background:#fff;color:#1e293b'"
              " onchange='document.getElementById(\"tzin\").value=this.value'>"
              "<option value=''>&#8613; choose a preset to fill the field below</option>"
              "<optgroup label='Universal'>"
              "<option value='UTC0'>UTC</option>"
              "</optgroup>"
              "<optgroup label='Europe'>"
              "<option value='GMT0BST,M3.5.0/1,M10.5.0'>London (GMT/BST)</option>"
              "<option value='WET0WEST,M3.5.0/1,M10.5.0'>Lisbon (WET/WEST)</option>"
              "<option value='CET-1CEST,M3.5.0,M10.5.0/3'>Central Europe (CET/CEST)</option>"
              "<option value='EET-2EEST,M3.5.0/3,M10.5.0/4'>Eastern Europe (EET/EEST)</option>"
              "<option value='MSK-3'>Moscow (MSK)</option>"
              "</optgroup>"
              "<optgroup label='Middle East / Asia'>"
              "<option value='AST-3'>Riyadh (AST +3)</option>"
              "<option value='GST-4'>Dubai (GST +4)</option>"
              "<option value='IST-5:30'>India (IST +5:30)</option>"
              "<option value='ICT-7'>Bangkok (ICT +7)</option>"
              "<option value='CST-8'>Beijing / Singapore (+8)</option>"
              "<option value='JST-9'>Tokyo (JST +9)</option>"
              "</optgroup>"
              "<optgroup label='Oceania'>"
              "<option value='AEST-10AEDT,M10.1.0,M4.1.0/3'>Sydney (AEST/AEDT)</option>"
              "<option value='NZST-12NZDT,M9.5.0,M4.1.0/3'>Auckland (NZST/NZDT)</option>"
              "</optgroup>"
              "<optgroup label='Americas'>"
              "<option value='EST5EDT,M3.2.0,M11.1.0'>US Eastern (EST/EDT)</option>"
              "<option value='CST6CDT,M3.2.0,M11.1.0'>US Central (CST/CDT)</option>"
              "<option value='MST7MDT,M3.2.0,M11.1.0'>US Mountain (MST/MDT)</option>"
              "<option value='PST8PDT,M3.2.0,M11.1.0'>US Pacific (PST/PDT)</option>"
              "<option value='AKST9AKDT,M3.2.0,M11.1.0'>Alaska (AKST/AKDT)</option>"
              "<option value='HST10'>Hawaii (HST &minus;10)</option>"
              "<option value='BRT3'>Brazil / Brasilia (BRT &minus;3)</option>"
              "<option value='ART3'>Argentina (ART &minus;3)</option>"
              "</optgroup>"
              "</select>"
              "<input id='tzin' name='tz' type='text' value='");
    html += curTz;
    html += F("' placeholder='e.g. CET-1CEST,M3.5.0,M10.5.0/3'"
              " style='display:block;width:100%;padding:6px 8px;border:1px solid #cbd5e1;"
              "border-radius:6px;font-family:monospace;font-size:.85rem;margin-bottom:10px'>"
              "<button class='btn btn-primary' type='submit'>Apply</button>"
              "</form></div>");

    // Hostname
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#127991;&#65039; Hostname</h3>"
              "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
              "Used for DHCP and the WiFi captive-portal AP name. "
              "Takes full effect after reboot.</p>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='hostname'>"
              "<div style='display:flex;gap:8px;align-items:center;margin-bottom:10px'>"
              "<input name='hostname' type='text' value='");
    html += curHostname;
    html += F("' placeholder='infoclock32' maxlength='63'"
              " style='flex:1;padding:6px 8px;border:1px solid #cbd5e1;border-radius:6px;"
              "font-family:monospace;font-size:.9rem'>"
              "</div>"
              "<p style='font-size:.8rem;color:#94a3b8;margin-bottom:10px'>"
              "Allowed: letters, digits and hyphens. Must not start or end with a hyphen.</p>"
              "<button class='btn btn-primary' type='submit'>Set</button>"
              "</form></div>");

    // Reboot shortcut
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#128260; Reboot</h3>"
              "<form method='POST' action='/reboot'>"
              "<button class='btn btn-danger' type='submit'>Reboot device</button>"
              "</form></div>");

    html += PAGE_FOOT;
    server.send(200, "text/html", html);
}

// ── /update (OTA firmware) ────────────────────────────────────────────────────

void handle_update_get()
{
    if (!is_authenticated()) return;

    String html = pageHead("Firmware update");
    html += pageNav("/update");
    html += F("<h2>&#128190; Firmware update</h2>"
              "<div class='card'>"
              "<p style='color:#475569;margin-bottom:16px'>"
              "Upload a compiled <code>.bin</code> file to flash new firmware. "
              "The device will reboot automatically on success.</p>"
              "<form method='POST' action='/update' enctype='multipart/form-data'>"
              "<input type='file' name='firmware' accept='.bin' "
              "style='display:block;margin-bottom:14px'>"
              "<button class='btn btn-primary' type='submit'>&#9654; Flash firmware</button>"
              "</form></div>");
    html += PAGE_FOOT;
    server.send(200, "text/html", html);
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
    String html = pageHead(ok ? "Update complete" : "Update failed");
    html += pageNav("/update");
    if (ok)
    {
        html += F("<h2>&#10003; Update complete</h2>"
                  "<div class='card'>"
                  "<p style='color:#15803d'>Firmware flashed successfully. Rebooting&hellip;</p>"
                  "</div>");
    }
    else
    {
        html += F("<h2>&#9888; Update failed</h2>"
                  "<div class='card'>"
                  "<p style='color:#dc2626'>Error: ");
        html += Update.errorString();
        html += F("</p><div class='actions'>"
                  "<a class='btn btn-primary' href='/update'>&#8592; Try again</a>"
                  "</div></div>");
    }
    html += PAGE_FOOT;
    server.send(200, "text/html", html);

    if (ok)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        ESP.restart();
    }
}

// ── /messages (custom message editor) ────────────────────────────────────────

static String htmlEsc(const String& s)
{
    String out;
    out.reserve(s.length() + 8);
    for (unsigned i = 0; i < s.length(); i++) {
        switch (s[i]) {
            case '&': out += F("&amp;");  break;
            case '<': out += F("&lt;");   break;
            case '>': out += F("&gt;");   break;
            case '"': out += F("&quot;"); break;
            default:  out += s[i];        break;
        }
    }
    return out;
}

// Returns true if name is valid for a slot: alphanumeric, _ or -, non-empty, max 32 chars.
static bool validSlotName(const String& name)
{
    if (name.isEmpty() || name.length() > 32) return false;
    for (unsigned i = 0; i < name.length(); i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    }
    return true;
}

// Collect all slot names from DataStore (keys of the form "message_<name>_text").
static std::vector<std::string> get_slot_names(DataStore& ds)
{
    std::vector<std::string> names;
    auto keys = ds.get_keys_with_prefix("message_");
    for (const auto& key : keys) {
        const std::string suffix = "_text";
        if (key.size() < 8 + 1 + suffix.size()) continue;
        if (key.substr(key.size() - suffix.size()) != suffix) continue;
        std::string name = key.substr(8, key.size() - 13);
        if (!ds.get_value(key, "").empty())
            names.push_back(name);
    }
    return names;
}

void handle_messages()
{
    if (!is_authenticated()) return;

    auto& ds = DataStore::getInstance();
    String result;
    bool  resultOk = true;

    if (server.method() == HTTP_POST)
    {
        // Interval
        int ivVal = server.arg("interval").toInt();
        if (ivVal >= 10)
            ds.set_value("msg_interval", std::to_string(ivVal));

        // Existing slots: form sends hidden n0=name, n1=name… and count=N,
        // plus t0/s0/e0/c0 per slot.
        int count = server.arg("count").toInt();
        for (int i = 0; i < count; i++)
        {
            String name = server.arg(String("n") + i);
            if (!validSlotName(name)) continue;

            std::string pfx = "message_" + std::string(name.c_str());
            String text = server.arg(String("t") + i);

            if (text.isEmpty())
            {
                // Empty text → delete this slot
                ds.remove_value(pfx + "_text");
                ds.remove_value(pfx + "_start");
                ds.remove_value(pfx + "_end");
                ds.remove_value(pfx + "_countdown");
            }
            else
            {
                ds.set_value(pfx + "_text",      text.c_str());
                ds.set_value(pfx + "_start",     server.arg(String("s") + i).c_str());
                ds.set_value(pfx + "_end",       server.arg(String("e") + i).c_str());
                ds.set_value(pfx + "_countdown", server.arg(String("c") + i).c_str());
            }
        }

        // New slot
        String newName = server.arg("newname");
        newName.trim();
        for (unsigned i = 0; i < newName.length(); i++)
            if (newName[i] == ' ') newName[i] = '_';

        String newText = server.arg("newtext");
        if (!newName.isEmpty() || !newText.isEmpty())
        {
            if (!validSlotName(newName))
            {
                result   = "&#9888; Invalid name &ldquo;" + htmlEsc(newName) +
                           "&rdquo; &mdash; use letters, digits, _ or -.";
                resultOk = false;
            }
            else if (newText.isEmpty())
            {
                result   = "&#9888; Text is required for new slot.";
                resultOk = false;
            }
            else
            {
                std::string pfx = "message_" + std::string(newName.c_str());
                ds.set_value(pfx + "_text",      newText.c_str());
                ds.set_value(pfx + "_start",     server.arg("newstart").c_str());
                ds.set_value(pfx + "_end",       server.arg("newend").c_str());
                ds.set_value(pfx + "_countdown", server.arg("newcountdown").c_str());
            }
        }

        if (resultOk)
        {
            ds.save_to_file("/config.txt");
            logPrintf("WEB", "custom messages saved via /messages");
            result = "&#10003; Saved.";
        }
    }

    // ── build page ────────────────────────────────────────────────────────────
    String interval   = ds.get_value("msg_interval", "60").c_str();
    auto   slotNames  = get_slot_names(ds);

    String html = pageHead("Messages",
        "<style>"
        "input[type=text].mt{width:100%;padding:5px 7px;border:1px solid #cbd5e1;"
        "border-radius:4px;font-size:.85rem;box-sizing:border-box}"
        "input[type=date].md{width:100%;padding:5px 4px;border:1px solid #cbd5e1;"
        "border-radius:4px;font-size:.8rem;box-sizing:border-box}"
        "td.sn{color:#64748b;font-weight:600;white-space:nowrap}"
        "th,td{padding:7px 10px}"
        "</style>");
    html += pageNav("/messages");
    html += F("<h2>&#128172; Custom messages</h2>");

    if (!result.isEmpty())
    {
        html += F("<div class='card' style='border-left:3px solid ");
        html += resultOk ? F("#15803d") : F("#dc2626");
        html += F(";margin-bottom:16px'><p style='font-weight:500;color:");
        html += resultOk ? F("#15803d") : F("#dc2626");
        html += F("'>");
        html += result;
        html += F("</p></div>");
    }

    html += F("<form method='POST'>");

    // Hidden slot names & count for the POST round-trip
    html += F("<input type='hidden' name='count' value='");
    html += (int)slotNames.size();
    html += F("'>");
    for (int i = 0; i < (int)slotNames.size(); i++)
    {
        html += F("<input type='hidden' name='n"); html += i;
        html += F("' value='"); html += htmlEsc(slotNames[i].c_str()); html += F("'>");
    }

    // Interval card
    html += F("<div class='card' style='margin-bottom:16px'>"
              "<h3 style='font-size:.95rem;font-weight:600;margin-bottom:10px'>Cycle interval</h3>"
              "<div style='display:flex;align-items:center;gap:8px'>"
              "<input name='interval' type='number' min='10' value='");
    html += interval;
    html += F("' style='width:80px;padding:6px 8px;border:1px solid #cbd5e1;"
              "border-radius:6px;font-size:.9rem'>"
              " <span style='color:#475569'>seconds between cycles</span></div></div>");

    // Existing slots table
    if (!slotNames.empty())
    {
        html += F("<div class='card' style='margin-bottom:16px'>"
                  "<p style='color:#64748b;font-size:.82rem;margin-bottom:10px'>"
                  "Clear <b>Text</b> and save to delete a slot. "
                  "Use <code>{}</code> for countdown days; "
                  "<code>{key}</code> inserts any config value "
                  "(e.g. <code>{hostname}</code>, <code>{location}</code>).</p>"
                  "<div style='overflow-x:auto'>"
                  "<table style='table-layout:fixed;min-width:560px'>"
                  "<colgroup><col style='width:130px'><col>"
                  "<col style='width:110px'><col style='width:110px'><col style='width:110px'>"
                  "</colgroup>"
                  "<tr><th>Name</th><th>Text</th>"
                  "<th title='Show from'>Start</th>"
                  "<th title='Hide after'>End</th>"
                  "<th title='Countdown target'>Countdown</th></tr>\n");

        for (int i = 0; i < (int)slotNames.size(); i++)
        {
            const std::string& name = slotNames[i];
            std::string pfx = "message_" + name;
            String text  = htmlEsc(ds.get_value(pfx + "_text",      "").c_str());
            String start =         ds.get_value(pfx + "_start",     "").c_str();
            String end   =         ds.get_value(pfx + "_end",       "").c_str();
            String cntdn =         ds.get_value(pfx + "_countdown", "").c_str();

            html += F("<tr><td class='sn'>"); html += htmlEsc(name.c_str());
            html += F("</td><td><input class='mt' type='text' name='t");
            html += i; html += F("' value='"); html += text; html += F("' maxlength='128'></td>");

            html += F("<td><input class='md' type='date' name='s");
            html += i; html += F("' value='"); html += start; html += F("'></td>");

            html += F("<td><input class='md' type='date' name='e");
            html += i; html += F("' value='"); html += end; html += F("'></td>");

            html += F("<td><input class='md' type='date' name='c");
            html += i; html += F("' value='"); html += cntdn; html += F("'></td></tr>\n");
        }
        html += F("</table></div></div>");
    }

    // Add new slot card
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#10133; Add slot</h3>"
              "<div style='display:grid;gap:8px'>"
              "<div style='display:flex;gap:8px;align-items:center'>"
              "<label style='font-size:.85rem;color:#475569;width:70px'>Name</label>"
              "<input class='mt' type='text' name='newname' maxlength='32' "
              "placeholder='e.g. lhc or party_2026' style='max-width:220px'></div>"
              "<div style='display:flex;gap:8px;align-items:center'>"
              "<label style='font-size:.85rem;color:#475569;width:70px'>Text</label>"
              "<input class='mt' type='text' name='newtext' maxlength='128' "
              "placeholder='Message text (use {} for countdown days)'></div>"
              "<div style='display:flex;gap:8px;align-items:center;flex-wrap:wrap'>"
              "<label style='font-size:.85rem;color:#475569;width:70px'>Start</label>"
              "<input class='md' type='date' name='newstart' style='width:130px'>"
              "<label style='font-size:.85rem;color:#475569;margin-left:8px'>End</label>"
              "<input class='md' type='date' name='newend' style='width:130px'>"
              "<label style='font-size:.85rem;color:#475569;margin-left:8px'>Countdown</label>"
              "<input class='md' type='date' name='newcountdown' style='width:130px'>"
              "</div></div></div>");

    html += F("<div class='actions'>"
              "<button class='btn btn-primary' type='submit'>&#128190; Save</button>"
              "</div></form>");
    html += PAGE_FOOT;
    server.send(200, "text/html", html);
}
