#include <Arduino.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include <data_store.hpp>
#include <version.hpp>
#include <web_ui.hpp>

// ── Server instance ───────────────────────────────────────────────────────────

WebServer server(80);

WebServer* getWebServer() { return &server; }

// ── Shared CSS ────────────────────────────────────────────────────────────────

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
.meta{font-weight:400;color:#94a3b8;font-size:.85rem}
.card-title{font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px}
.form-input{width:100%;padding:8px 10px;border:1px solid #cbd5e1;
            border-radius:6px;font-size:.9rem;margin-bottom:10px;
            background:#fff;color:#1e293b}
.flex-between{display:flex;align-items:center;gap:12px;margin-bottom:10px}
.flex-between input{flex:1}
.flex-between span{font-family:monospace;min-width:2ch}
)css";

static const char PAGE_FOOT[] PROGMEM =
    "</main>"
    "<footer style='text-align:center;padding:10px 0 14px;"
    "font-size:.75rem;color:#94a3b8'>"
    APP_VERSION " &bull; built " BUILD_DATE " " BUILD_TIME
    "</footer>"
    "</body></html>";

// ── CSS endpoint ──────────────────────────────────────────────────────────────

void handle_style_css()
{
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "text/css", CSS);
}

// ── Status helpers ────────────────────────────────────────────────────────────

void getStatusFields(char* uptime, size_t uptime_sz,
                     char* heap, size_t heap_sz,
                     char* rssi, size_t rssi_sz)
{
    unsigned long ms = millis();
    snprintf(uptime, uptime_sz, "%luh %lum %lus",
             ms / 3600000UL, (ms % 3600000UL) / 60000UL, (ms % 60000UL) / 1000UL);

    snprintf(heap, heap_sz, "%u KB  (%u B)",
             (unsigned)esp_get_free_heap_size() / 1024,
             (unsigned)esp_get_free_heap_size());

    int rssiVal = WiFi.RSSI();
    const char* quality = rssiVal >= -60 ? "excellent" : rssiVal >= -70 ? "good"
                        : rssiVal >= -80 ? "fair" : "weak";
    snprintf(rssi, rssi_sz, "%d dBm (%s)", rssiVal, quality);
}

// ── Layout helpers ────────────────────────────────────────────────────────────

void sendPageHead(const char* title, const char* extraHead)
{
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent_P(PSTR("<!DOCTYPE html><html lang='en'><head>"
                               "<meta charset='utf-8'>"
                               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                               "<title>"));
    server.sendContent(title);
    server.sendContent_P(PSTR(" \xe2\x80\x94 "));
    server.sendContent(WiFi.getHostname());
    server.sendContent_P(PSTR("</title>"
        "<link rel='stylesheet' href='/style.css?v=" APP_VERSION "'>"));
    if (extraHead[0]) server.sendContent(extraHead);
    server.sendContent_P(PSTR("</head><body>"
                               "<header><h1>&#128336; "));
    server.sendContent(WiFi.getHostname());
    server.sendContent_P(PSTR("</h1><span>"));
    server.sendContent(WiFi.localIP().toString().c_str());
    server.sendContent_P(PSTR("</span></header>"));
}

void sendPageNav(const char* active)
{
    struct { const char* href; const char* label; } links[] = {
        {"/",         "Home"},
        {"/status",   "Status"},
        {"/log",      "Log"},
        {"/edit",     "Config"},
        {"/actions",  "Actions"},
        {"/messages", "Messages"},
        {"/update",   "Update"},
    };
    server.sendContent_P(PSTR("<nav>"));
    for (auto& l : links) {
        server.sendContent_P(PSTR("<a href='"));
        server.sendContent(l.href);
        if (strcmp(l.href, active) == 0)
            server.sendContent_P(PSTR("' class='active'>"));
        else
            server.sendContent_P(PSTR("'>"));
        server.sendContent(l.label);
        server.sendContent_P(PSTR("</a>"));
    }
    server.sendContent_P(PSTR("</nav><main>"));
}

void sendPageFoot()
{
    server.sendContent_P(PAGE_FOOT);
}

void sendRow(const char* label, const char* value)
{
    server.sendContent_P(PSTR("<tr><td class='label'>"));
    server.sendContent(label);
    server.sendContent_P(PSTR("</td><td>"));
    server.sendContent(value);
    server.sendContent_P(PSTR("</td></tr>\n"));
}

// ── Auth ──────────────────────────────────────────────────────────────────────

// Cached password — avoids DataStore lookup + mbedtls decode on every request.
static std::string s_cachedPw;
static bool        s_pwLoaded = false;

void invalidate_auth_cache() { s_pwLoaded = false; }

bool check_auth_header()
{
    if (!s_pwLoaded) {
        s_cachedPw = DataStore::getInstance().get_value("web_password", "");
        s_pwLoaded = true;
    }
    const std::string& pw = s_cachedPw;
    if (pw.empty()) return true;   // no password set → open access

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

    // Username is ignored — compare only the part after the colon.
    String credentials((char*)decoded);
    int colon = credentials.indexOf(':');
    String submitted = (colon != -1) ? credentials.substring(colon + 1) : credentials;
    return submitted == pw.c_str();
}

bool is_authenticated()
{
    if (check_auth_header()) return true;
    server.sendHeader("WWW-Authenticate", "Basic realm=\"infoclock32\"");
    server.send(401, "text/plain", "Unauthorized");
    return false;
}
