#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <timezone_utils.hpp>
#include <web_ui.hpp>

// ── /push ─────────────────────────────────────────────────────────────────────
// Simple JSON endpoint for scripted / programmatic message display.
//
//   GET  /push?msg=Hello+World
//   POST /push   (body: msg=Hello+World)
//
// Optional: &speed=<ms>  — scroll delay in ms, 10–500 (default 50).
// Returns: {"ok":true} or {"ok":false,"error":"..."}
// No authentication required.

void handle_push()
{
    String msg = server.hasArg("msg") ? server.arg("msg") : server.arg("message");
    if (msg.isEmpty())
    {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"missing 'msg' parameter\"}");
        return;
    }

    int speed = 50;
    if (server.hasArg("speed"))
    {
        int s = server.arg("speed").toInt();
        if (s >= 10 && s <= 500) speed = s;
    }

    auto& rmd = ResourceManager<LMDS>::getInstance();
    if (rmd.make_access_request())
    {
        scrollMessage(std::string(msg.c_str()), rmd.getResourceRef(), speed);
        rmd.release_access();
        logPrintf("WEB", "push via /push: %.48s", msg.c_str());
        server.send(200, "application/json", "{\"ok\":true}");
    }
    else
    {
        server.send(503, "application/json",
                    "{\"ok\":false,\"error\":\"display busy\"}");
    }
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
                MDNS.end();
                MDNS.begin(hn.c_str());
                MDNS.addService("http", "tcp", 80);
                result = "&#10003; Hostname set to &ldquo;" + hn + "&rdquo;. Now reachable at <a href='http://" + hn + ".local'>" + hn + ".local</a>.";
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
        else if (action == "reset_display")
        {
            if (rmd.make_access_request())
            {
                auto& matrix = rmd.getResourceRef();
                matrix.begin();
                int level = DataStore::getInstance().get_value<int>("brightness", 7);
                matrix.setIntensity((uint8_t)level);
                rmd.release_access();
                result = "&#10003; Display reset.";
                logPrintf("WEB", "display reset via /actions");
            }
            else { result = "&#9888; Display busy &mdash; try again."; }
        }
        else if (action == "password")
        {
            String pw = server.arg("web_password");
            DataStore::getInstance().set_value("web_password", pw.c_str());
            DataStore::getInstance().save_to_file("/config.txt");
            if (pw.isEmpty())
                result = "&#10003; Password cleared &mdash; web UI is now open.";
            else
                result = "&#10003; Password updated.";
            logPrintf("WEB", "web_password %s via /actions", pw.isEmpty() ? "cleared" : "changed");
        }
    }

    auto& ds = DataStore::getInstance();
    char brightStr[4];
    snprintf(brightStr, sizeof(brightStr), "%d",
             ds.get_value<int>("brightness", 7));
    String curTz       = ds.get_value("timezone", "UTC0").c_str();
    String curHostname = WiFi.getHostname();
    String nStart      = ds.get_value("night_start", "").c_str();
    String nEnd        = ds.get_value("night_end",   "").c_str();
    char   nBrStr[4];
    snprintf(nBrStr, sizeof(nBrStr), "%d", ds.get_value<int>("night_brightness", 1));

    sendPageHead("Actions");
    sendPageNav("/actions");
    server.sendContent_P(PSTR("<h2>Actions</h2>"));

    if (!result.isEmpty())
    {
        server.sendContent_P(PSTR("<div class='card' style='border-left:3px solid #15803d;"
                                   "margin-bottom:20px'>"
                                   "<p style='color:#15803d;font-weight:500'>"));
        server.sendContent(result.c_str());
        server.sendContent_P(PSTR("</p></div>"));
    }

    // Push message
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128172; Push message</h3>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='push'>"
                               "<input name='message' type='text' placeholder='Message to scroll&hellip;' "
                               "style='width:100%;padding:8px 10px;border:1px solid #cbd5e1;border-radius:6px;"
                               "font-size:.9rem;margin-bottom:10px;background:#fff;color:#1e293b'>"
                               "<button class='btn btn-primary' type='submit'>&#9654; Send</button>"
                               "</form></div>"));

    // Brightness
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#9728;&#65039; Brightness (0&ndash;15)</h3>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='brightness'>"
                               "<div style='display:flex;align-items:center;gap:12px;margin-bottom:10px'>"
                               "<input name='level' type='range' min='0' max='15' value='"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("' style='flex:1' oninput='this.nextElementSibling.textContent=this.value'>"
                               "<span style='font-family:monospace;min-width:2ch'>"));
    server.sendContent(brightStr);
    server.sendContent_P(PSTR("</span></div>"
                               "<button class='btn btn-primary' type='submit'>Set</button>"
                               "</form></div>"));

    // Night mode
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#127769; Night mode</h3>"
                               "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                               "Dims the display between two times. Leave blank to disable.</p>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='nightmode'>"
                               "<div style='display:flex;gap:16px;align-items:center;flex-wrap:wrap;margin-bottom:10px'>"
                               "<label style='font-size:.9rem'>From</label>"
                               "<input type='time' name='night_start' value='"));
    if (!nStart.isEmpty()) server.sendContent(nStart.c_str());
    server.sendContent_P(PSTR("' style='padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                               "<label style='font-size:.9rem'>To</label>"
                               "<input type='time' name='night_end' value='"));
    if (!nEnd.isEmpty()) server.sendContent(nEnd.c_str());
    server.sendContent_P(PSTR("' style='padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                               "<label style='font-size:.9rem'>Brightness</label>"
                               "<input type='number' name='night_brightness' min='0' max='15' value='"));
    server.sendContent(nBrStr);
    server.sendContent_P(PSTR("' style='width:60px;padding:5px 8px;border:1px solid #cbd5e1;border-radius:6px'>"
                               "</div>"
                               "<button class='btn btn-primary' type='submit'>Save</button>"
                               "</form></div>"));

    // Timezone
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#127760; Timezone</h3>"
                               "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                               "Pick a preset, or enter a POSIX TZ string directly. "
                               "Applies immediately&nbsp;&mdash; no reboot needed.</p>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='timezone'>"
                               "<select style='display:block;width:100%;padding:6px 8px;"
                               "border:1px solid #cbd5e1;border-radius:6px;font-size:.9rem;"
                               "margin-bottom:8px;background:#fff;color:#1e293b'"
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
                               "<input id='tzin' name='tz' type='text' value='"));
    server.sendContent(curTz.c_str());
    server.sendContent_P(PSTR("' placeholder='e.g. CET-1CEST,M3.5.0,M10.5.0/3'"
                               " style='display:block;width:100%;padding:6px 8px;"
                               "border:1px solid #cbd5e1;border-radius:6px;"
                               "font-family:monospace;font-size:.85rem;margin-bottom:10px'>"
                               "<button class='btn btn-primary' type='submit'>Apply</button>"
                               "</form></div>"));

    // Hostname
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#127991;&#65039; Hostname</h3>"
                               "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                               "Used for DHCP and the WiFi captive-portal AP name. "
                               "Takes full effect after reboot.</p>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='hostname'>"
                               "<div style='display:flex;gap:8px;align-items:center;margin-bottom:10px'>"
                               "<input name='hostname' type='text' value='"));
    server.sendContent(curHostname.c_str());
    server.sendContent_P(PSTR("' placeholder='infoclock32' maxlength='63'"
                               " style='flex:1;padding:6px 8px;border:1px solid #cbd5e1;"
                               "border-radius:6px;font-family:monospace;font-size:.9rem'>"
                               "</div>"
                               "<p style='font-size:.8rem;color:#94a3b8;margin-bottom:10px'>"
                               "Allowed: letters, digits and hyphens. Must not start or end with a hyphen.</p>"
                               "<button class='btn btn-primary' type='submit'>Set</button>"
                               "</form></div>"));

    // Password
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128274; Web password</h3>"
                               "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                               "Protects all authenticated pages. Leave blank to disable authentication.</p>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='password'>"
                               "<input name='web_password' type='password' placeholder='New password'"
                               " style='width:100%;padding:6px 8px;border:1px solid #cbd5e1;"
                               "border-radius:6px;font-size:.9rem;margin-bottom:10px;box-sizing:border-box'>"
                               "<button class='btn btn-primary' type='submit'>Set password</button>"
                               "</form></div>"));

    // Reset display
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128165; Reset display</h3>"
                               "<p style='font-size:.85rem;color:#64748b;margin-bottom:12px'>"
                               "Re-initialises the MAX7219 chip. Use if the display looks glitched.</p>"
                               "<form method='POST'>"
                               "<input type='hidden' name='action' value='reset_display'>"
                               "<button class='btn btn-primary' type='submit'>Reset display</button>"
                               "</form></div>"));

    // Reboot
    server.sendContent_P(PSTR("<div class='card'>"
                               "<h3 style='font-size:.95rem;font-weight:600;color:#1e293b;margin-bottom:12px'>"
                               "&#128260; Reboot</h3>"
                               "<form method='POST' action='/reboot'>"
                               "<button class='btn btn-danger' type='submit'>Reboot device</button>"
                               "</form></div>"));

    sendPageFoot();
}
