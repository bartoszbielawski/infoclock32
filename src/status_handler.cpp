#include <Arduino.h>
#include <data_store.hpp>
#include <logger.hpp>
#include <web_ui.hpp>
#include <security_utils.hpp>
#include <status_task.h>

// ── Timed status: web page and HTTP endpoints ────────────────────────────────
//
//   GET  /s                 phone-friendly page: one big button per preset
//   GET|POST /status/set    preset=<slot> | text=<...>[&cue=][&minutes=]
//   GET|POST /status/cancel
//   GET  /status/info       {"active":..,"text":..,"remaining_s":..}
//
// `minutes` is a number (1..720) or "eoh" (until the end of the hour).
// Like /push these are open unless `push_token` is set, in which case every
// request must carry &token=<push_token>. Plain HTTP: anyone on the network
// who can reach the device can set a status - keep push_token set if that
// matters. `ui=1` (sent by the page's forms) answers with a redirect back to
// /s instead of JSON.

static String htmlEsc(const String& s)
{
    String out;
    out.reserve(s.length() + 8);
    for (unsigned i = 0; i < s.length(); i++)
    {
        switch (s[i])
        {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:  out += s[i];     break;
        }
    }
    return out;
}

static String jsonEsc(const std::string& s)
{
    String out;
    for (char c : s)
    {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

static bool token_ok()
{
    return pushTokenOk(DataStore::getInstance().get_value("push_token", ""),
                       server.arg("token").c_str());
}

static bool require_token()
{
    if (token_ok()) return true;
    logPrintf("WEB", "status request refused: bad or missing token");
    server.send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden\"}");
    return false;
}

// Back to the page the form came from, keeping the token in the URL.
static void redirect_to_page()
{
    String loc = "/s";
    if (server.arg("token").length())
    {
        loc += "?token=";
        for (unsigned i = 0; i < server.arg("token").length(); i++)
        {
            char c = server.arg("token")[i];
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~')
                loc += c;
            else
            {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                loc += hex;
            }
        }
    }
    server.sendHeader("Location", loc);
    server.send(303, "text/plain", "");
}

static void send_error(StatusError e)
{
    String body = "{\"ok\":false,\"error\":\"";
    body += status_error_text(e);
    body += "\"}";
    server.send(e == StatusError::NoSuchPreset ? 404 : 400, "application/json", body);
}

void handle_status_set()
{
    if (!require_token()) return;

    std::string minutes = server.arg("minutes").c_str();
    StatusError e;
    if (server.hasArg("preset"))
        e = status_request_preset(server.arg("preset").toInt(), minutes);
    else if (server.hasArg("text"))
        e = status_request_custom(server.arg("text").c_str(), server.arg("cue").c_str(), minutes);
    else
    {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"need 'preset' or 'text'\"}");
        return;
    }

    if (e != StatusError::Ok)
    {
        logPrintf("WEB", "status refused: %s", status_error_text(e));
        send_error(e);
        return;
    }

    StatusInfo info = status_info();
    logPrintf("WEB", "status set: '%.40s' %lu s", info.text.c_str(), (unsigned long)info.totalS);
    if (server.hasArg("ui"))
    {
        redirect_to_page();
        return;
    }
    String body = "{\"ok\":true,\"remaining_s\":";
    body += String((unsigned long)info.remainingS);
    body += "}";
    server.send(200, "application/json", body);
}

void handle_status_cancel()
{
    if (!require_token()) return;
    status_cancel();
    logPrintf("WEB", "status cancelled");
    if (server.hasArg("ui"))
    {
        redirect_to_page();
        return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

void handle_status_info()
{
    if (!require_token()) return;
    StatusInfo info = status_info();
    String body = "{\"active\":";
    body += info.active ? "true" : "false";
    if (info.active)
    {
        body += ",\"text\":\"";
        body += jsonEsc(info.text);
        body += "\",\"cue\":\"";
        body += status_cue_name(info.cue);
        body += "\",\"remaining_s\":";
        body += String((unsigned long)info.remainingS);
        body += ",\"total_s\":";
        body += String((unsigned long)info.totalS);
    }
    body += "}";
    server.send(200, "application/json", body);
}

static void hidden_fields()
{
    server.sendContent_P(PSTR("<input type='hidden' name='ui' value='1'>"));
    if (server.arg("token").length())
    {
        server.sendContent_P(PSTR("<input type='hidden' name='token' value='"));
        server.sendContent(htmlEsc(server.arg("token")).c_str());
        server.sendContent_P(PSTR("'>"));
    }
}

void handle_status_page()
{
    if (!token_ok())
    {
        server.send(403, "text/plain", "Forbidden - append ?token=<push_token>");
        return;
    }

    StatusInfo info = status_info();

    sendPageHead("Presence",
        "<meta http-equiv='refresh' content='15'>"
        "<style>"
        ".big{display:block;width:100%;padding:22px 12px;margin:0 0 12px;font-size:1.4rem;"
        "font-weight:700;letter-spacing:.5px}"
        ".chips{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:16px}"
        ".chips label{flex:1 1 40%;text-align:center;padding:12px 8px;border:2px solid #cbd5e1;"
        "border-radius:8px;background:#fff;font-size:1rem;cursor:pointer}"
        ".chips input{position:absolute;opacity:0}"
        ".chips input:checked+span{font-weight:700;color:#1d4ed8}"
        ".chips label:has(input:checked){border-color:#1d4ed8;background:#eff6ff}"
        ".on{background:#fee2e2;border-left:6px solid #dc2626}"
        "</style>");
    sendPageNav("/s");

    if (info.active)
    {
        server.sendContent_P(PSTR("<div class='card on'><div class='card-title'>Showing now</div>"
                                  "<p style='font-size:1.6rem;font-weight:700'>"));
        server.sendContent(htmlEsc(info.text.c_str()).c_str());
        server.sendContent_P(PSTR("</p><p class='meta'>"));
        char left[48];
        snprintf(left, sizeof(left), "%lu min %02lu s left",
                 (unsigned long)(info.remainingS / 60), (unsigned long)(info.remainingS % 60));
        server.sendContent(left);
        server.sendContent_P(PSTR("</p><form method='POST' action='/status/cancel' style='margin-top:12px'>"));
        hidden_fields();
        server.sendContent_P(PSTR("<button class='btn btn-danger big' type='submit'>Cancel</button>"
                                  "</form></div>"));
    }

    server.sendContent_P(PSTR("<form method='POST' action='/status/set'>"));
    hidden_fields();
    server.sendContent_P(PSTR(
        "<div class='card'><div class='card-title'>For how long?</div><div class='chips'>"
        "<label><input type='radio' name='minutes' value='' checked><span>Preset default</span></label>"
        "<label><input type='radio' name='minutes' value='30'><span>30 min</span></label>"
        "<label><input type='radio' name='minutes' value='60'><span>1 hour</span></label>"
        "<label><input type='radio' name='minutes' value='120'><span>2 hours</span></label>"
        "<label><input type='radio' name='minutes' value='eoh'><span>Until end of hour</span></label>"
        "</div>"));
    for (const StatusPreset& p : status_presets())
    {
        server.sendContent_P(PSTR("<button class='btn btn-primary big' type='submit' name='preset' value='"));
        server.sendContent(String(p.slot).c_str());
        server.sendContent_P(PSTR("'>"));
        server.sendContent(htmlEsc(p.text.c_str()).c_str());
        server.sendContent_P(PSTR("</button>"));
    }
    server.sendContent_P(PSTR("</div></form>"));

    server.sendContent_P(PSTR(
        "<form method='POST' action='/status/set'>"));
    hidden_fields();
    server.sendContent_P(PSTR(
        "<details class='card'><summary class='card-title'>Custom text</summary>"
        "<input class='form-input' name='text' maxlength='64' placeholder='Text to show'>"
        "<select class='form-input' name='cue'>"
        "<option value='none'>No cue</option><option value='border'>Border</option>"
        "<option value='blink'>Blinking border</option><option value='invert'>Inverting</option>"
        "</select>"
        "<input class='form-input' name='minutes' value='60' placeholder='minutes or eoh'>"
        "<button class='btn btn-primary' type='submit'>Show</button>"
        "</details></form>"));
    sendPageFoot();
}
