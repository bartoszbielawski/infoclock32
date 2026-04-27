#include <Arduino.h>
#include <WiFi.h>
#include <web_ui.hpp>
#include <data_store.hpp>
#include <logger.hpp>

// ── helpers ───────────────────────────────────────────────────────────────────

// Returns a Unicode signal-strength indicator (4 block chars, UTF-8).
static const char* signal_bars(int rssi)
{
    if (rssi >= -60) return "\xe2\x96\x82\xe2\x96\x84\xe2\x96\x86\xe2\x96\x88"; // ▂▄▆█
    if (rssi >= -70) return "\xe2\x96\x82\xe2\x96\x84\xe2\x96\x86\xe2\x96\x91"; // ▂▄▆░
    if (rssi >= -80) return "\xe2\x96\x82\xe2\x96\x84\xe2\x96\x91\xe2\x96\x91"; // ▂▄░░
    return             "\xe2\x96\x82\xe2\x96\x91\xe2\x96\x91\xe2\x96\x91";      // ▂░░░
}

// ── GET /wifi ─────────────────────────────────────────────────────────────────

void handle_wifi_get()
{
    if (!is_authenticated()) return;

    if (server.hasArg("rescan")) {
        WiFi.scanNetworks(/*async=*/true);
        server.sendHeader("Location", "/wifi");
        server.send(303);
        return;
    }

    int n = WiFi.scanComplete();

    // Scan not done yet — show a spinner page that refreshes itself.
    if (n == WIFI_SCAN_RUNNING) {
        sendPageHead("WiFi Setup", "<meta http-equiv='refresh' content='2'>");
        sendPageNav("/wifi");
        server.sendContent_P(PSTR(
            "<div class='card'>"
            "<p style='color:#64748b'>Scanning for networks\xe2\x80\xa6</p>"
            "</div>"));
        sendPageFoot();
        return;
    }

    // No scan has been triggered yet (e.g. connected mode, first visit).
    if (n == WIFI_SCAN_FAILED || n < 0) {
        WiFi.scanNetworks(/*async=*/true);
        sendPageHead("WiFi Setup", "<meta http-equiv='refresh' content='2'>");
        sendPageNav("/wifi");
        server.sendContent_P(PSTR(
            "<div class='card'>"
            "<p style='color:#64748b'>Starting scan\xe2\x80\xa6</p>"
            "</div>"));
        sendPageFoot();
        return;
    }

    // Sort indices by RSSI descending.
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i]))
                std::swap(idx[i], idx[j]);

    sendPageHead("WiFi Setup", "");
    sendPageNav("/wifi");

    // Inline JS: clicking an SSID button fills the hidden field and shows the form.
    server.sendContent_P(PSTR(
        "<script>"
        "function pick(s){"
        "document.getElementById('ssid').value=s;"
        "document.getElementById('pwform').style.display='block';"
        "document.getElementById('pw').focus();"
        "}"
        "</script>"
        "<h2>WiFi Setup</h2>"
        "<div class='card'>"
        "<p style='margin-bottom:12px;color:#64748b;font-size:.9rem'>"
        "Select a network to connect to.</p>"
        "<div style='display:flex;flex-wrap:wrap;gap:8px;margin-bottom:20px'>"));

    for (int i = 0; i < n; i++) {
        int j = idx[i];
        String ssid = WiFi.SSID(j);
        int rssi    = WiFi.RSSI(j);
        bool open   = (WiFi.encryptionType(j) == WIFI_AUTH_OPEN);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "<button type='button' class='btn btn-primary' "
            "onclick=\"pick('%s')\" "
            "style='font-size:.85rem;padding:6px 12px' "
            "title='%d dBm'>%s %s%s</button>",
            ssid.c_str(),
            rssi,
            signal_bars(rssi),
            ssid.c_str(),
            open ? " \xf0\x9f\x94\x93" : "");   // open networks get a padlock emoji
        server.sendContent(buf);
    }

    server.sendContent_P(PSTR("</div>"));  // flex wrap

    // Password form — hidden until a network is picked.
    server.sendContent_P(PSTR(
        "<form id='pwform' method='POST' action='/wifi' "
        "style='display:none'>"
        "<input type='hidden' id='ssid' name='ssid'>"
        "<label style='display:block;margin-bottom:6px;font-size:.9rem;"
        "font-weight:500'>Password</label>"
        "<input id='pw' type='password' name='password' "
        "class='form-input' placeholder='Leave blank for open networks'>"
        "<div class='actions'>"
        "<button type='submit' class='btn btn-primary'>Save &amp; suggest reboot</button>"
        "</div>"
        "</form>"));

    // Rescan link.
    server.sendContent_P(PSTR(
        "<p style='margin-top:16px;font-size:.85rem'>"
        "<a href='/wifi?rescan=1' style='color:#1d4ed8'>&#8635; Rescan</a>"
        "</p>"
        "</div>"));  // card

    sendPageFoot();
}

// ── POST /wifi ────────────────────────────────────────────────────────────────

void handle_wifi_post()
{
    if (!is_authenticated()) return;

    String ssid     = server.arg("ssid");
    String password = server.arg("password");

    if (ssid.isEmpty()) {
        server.sendHeader("Location", "/wifi");
        server.send(303);
        return;
    }

    auto& ds = DataStore::getInstance();
    ds.set_value("wifi_ssid",     std::string(ssid.c_str()));
    ds.set_value("wifi_password", std::string(password.c_str()));
    ds.save_to_file("/config.txt");

    logPrintf("WEB", "WiFi credentials updated via /wifi, SSID=%s", ssid.c_str());

    sendPageHead("WiFi Setup", "");
    sendPageNav("/wifi");
    server.sendContent_P(PSTR(
        "<h2>WiFi Setup</h2>"
        "<div class='card'>"));

    char buf[128];
    snprintf(buf, sizeof(buf),
        "<p style='margin-bottom:12px'>Credentials saved for <strong>%s</strong>.</p>",
        ssid.c_str());
    server.sendContent(buf);

    server.sendContent_P(PSTR(
        "<p style='margin-bottom:16px;color:#64748b;font-size:.9rem'>"
        "Restart the device to connect with the new settings.</p>"
        "<form method='POST' action='/reboot'>"
        "<button type='submit' class='btn btn-danger'>Reboot now</button>"
        "</form>"
        "</div>"));

    sendPageFoot();
}

