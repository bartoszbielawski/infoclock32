#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <freertos/FreeRTOS.h>
#include <data_store.hpp>
#include <logger.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>

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
    h += F(" — infoclock32</title><style>");
    h += CSS;
    h += F("</style>");
    h += extraHead;
    h += F("</head><body>"
           "<header><h1>&#128336; infoclock32</h1>"
           "<span>");
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
        + link("/actions", "Actions")
        + F("</nav><main>");
}

static const char PAGE_FOOT[] PROGMEM = "</main></body></html>";

// ── auth ──────────────────────────────────────────────────────────────────────

bool is_authenticated()
{
    auto reject = [&]() {
        server.sendHeader("WWW-Authenticate", "Basic realm=\"infoclock32\"");
        server.send(401, "text/plain", "Unauthorized");
    };

    if (!server.hasHeader("Authorization")) { reject(); return false; }

    String authHeader = server.header("Authorization");
    if (!authHeader.startsWith("Basic ")) { reject(); return false; }

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
        return true;

    reject();
    return false;
}

// ── handlers ──────────────────────────────────────────────────────────────────

void handle_home();
void handle_file_edit();
void handle_reboot();
void handle_log();
void handle_status();
void handle_actions();

void web_server_task(void* pvParameters)
{
    server.on("/", handle_home);
    server.on("/status",  HTTP_GET,  handle_status);
    server.on("/edit",               handle_file_edit);
    server.on("/reboot",  HTTP_POST, handle_reboot);
    server.on("/log",     HTTP_GET,  handle_log);
    server.on("/actions",            handle_actions);
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

    auto row = [](const char* label, const String& value) -> String {
        return String(F("<tr><td class='label'>")) + label +
               F("</td><td>") + value + F("</td></tr>\n");
    };

    // ── page ──────────────────────────────────────────────────────────────────
    String html = pageHead("Home", "<meta http-equiv='refresh' content='30'>");
    html += pageNav("/");

    // Status table
    html += F("<h2>&#9881; Device status</h2>"
              "<table style='margin-bottom:24px'>"
              "<tr><th colspan='2'>&#127760; Network</th></tr>");
    html += row("IP address",  WiFi.localIP().toString());
    html += row("Hostname",    WiFi.getHostname());
    html += row("SSID",        WiFi.SSID());
    html += row("Signal",      rssiStr);
    html += F("<tr><th colspan='2'>&#128421; System</th></tr>");
    html += row("Uptime",    uptime);
    html += row("Free heap", heap);
    html += row("Chip",      ESP.getChipModel());
    html += F("</table>");

    // Action cards — forms POST to /actions
    html += F("<h2>&#9889; Actions</h2>");

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
              "&#9728; Brightness (0&ndash;15)</h3>"
              "<form method='POST' action='/actions'>"
              "<input type='hidden' name='action' value='brightness'>"
              "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
              "<input name='level' type='range' min='0' max='15' value='7' style='flex:1'"
              " oninput='this.nextElementSibling.textContent=this.value'>"
              "<span style='font-family:monospace;min-width:2ch'>7</span>"
              "</div>"
              "<button class='btn btn-primary' type='submit'>Set</button>"
              "</form></div>");

    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#9211; Display power &amp; reboot</h3>"
              "<div class='actions'>"
              "<form method='POST' action='/actions' style='display:contents'>"
              "<input type='hidden' name='action' value='power'>"
              "<button class='btn btn-primary' name='state' value='on'  type='submit'>&#9654; On</button>"
              "<button class='btn btn-danger'  name='state' value='off' type='submit'>&#9632; Off</button>"
              "</form>"
              "<form method='POST' action='/reboot' style='display:contents'>"
              "<button class='btn btn-danger' type='submit'>&#128260; Reboot</button>"
              "</form>"
              "</div></div>");

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
    html += F("<tr><th colspan='2'>&#9881; System</th></tr>");
    html += row("Uptime",      uptime);
    html += row("Free heap",   heap);
    html += row("Chip",        ESP.getChipModel());
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
            html += F("<tr><td class='mono' style='white-space:nowrap'>") + ts + F("</td>"
                      "<td><span class='tag tag-info'>") + tag + F("</span></td>"
                      "<td class='mono'>") + msg + F("</td></tr>\n");
        }
        else
        {
            html += F("<tr><td colspan='3' class='mono'>") + line + F("</td></tr>\n");
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
                    result = "&#10003; Brightness set to " + String(level) + ".";
                    logPrintf("WEB", "brightness set to %d via /actions", level);
                }
                else { result = "&#9888; Display busy &mdash; try again."; }
            }
        }
        else if (action == "power")
        {
            bool on = (server.arg("state") == "on");
            if (rmd.make_access_request())
            {
                auto& matrix = rmd.getResourceRef();
                matrix.setEnabled(on);
                matrix.display();
                rmd.release_access();
                result = String("&#10003; Display powered ") + (on ? "on" : "off") + ".";
                logPrintf("WEB", "display powered %s via /actions", on ? "on" : "off");
            }
            else { result = "&#9888; Display busy &mdash; try again."; }
        }
    }

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
              "&#9728; Brightness (0&ndash;15)</h3>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='brightness'>"
              "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
              "<input name='level' type='range' min='0' max='15' value='7' style='flex:1'"
              " oninput='this.nextElementSibling.textContent=this.value'>"
              "<span style='font-family:monospace;min-width:2ch'>7</span>"
              "</div>"
              "<button class='btn btn-primary' type='submit'>Set</button>"
              "</form></div>");

    // Power
    html += F("<div class='card'>"
              "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
              "&#9211; Display power</h3>"
              "<form method='POST'>"
              "<input type='hidden' name='action' value='power'>"
              "<div class='actions'>"
              "<button class='btn btn-primary' name='state' value='on'  type='submit'>&#9654; On</button>"
              "<button class='btn btn-danger'  name='state' value='off' type='submit'>&#9632; Off</button>"
              "</div></form></div>");

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
