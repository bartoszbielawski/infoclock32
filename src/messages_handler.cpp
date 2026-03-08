#include <Arduino.h>
#include <data_store.hpp>
#include <logger.hpp>
#include <web_ui.hpp>
#include <custom_message.hpp>

// ── Helpers ───────────────────────────────────────────────────────────────────

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

static bool validSlotName(const String& name)
{
    if (name.isEmpty() || name.length() > 32) return false;
    for (unsigned i = 0; i < name.length(); i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    }
    return true;
}

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

// ── Handler ───────────────────────────────────────────────────────────────────

void handle_messages()
{
    if (!is_authenticated()) return;

    auto& ds = DataStore::getInstance();
    String result;
    bool   resultOk = true;

    if (server.method() == HTTP_POST)
    {
        int ivVal = server.arg("interval").toInt();
        if (ivVal >= 10)
            ds.set_value("msg_interval", std::to_string(ivVal));

        int count = server.arg("count").toInt();
        for (int i = 0; i < count; i++)
        {
            String name = server.arg(String("n") + i);
            if (!validSlotName(name)) continue;

            std::string pfx  = "message_" + std::string(name.c_str());
            String      text = server.arg(String("t") + i);

            if (text.isEmpty())
            {
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

    String interval  = ds.get_value("msg_interval", "60").c_str();
    auto   slotNames = get_slot_names(ds);

    sendPageHead("Messages",
        "<style>"
        "input[type=text].mt{width:100%;padding:5px 7px;border:1px solid #cbd5e1;"
        "border-radius:4px;font-size:.85rem;box-sizing:border-box}"
        "input[type=date].md{width:100%;padding:5px 4px;border:1px solid #cbd5e1;"
        "border-radius:4px;font-size:.8rem;box-sizing:border-box}"
        "td.sn{color:#64748b;font-weight:600;white-space:nowrap}"
        "th,td{padding:7px 10px}"
        "</style>");
    sendPageNav("/messages");
    server.sendContent_P(PSTR("<h2>&#128172; Custom messages</h2>"));

    if (!result.isEmpty())
    {
        server.sendContent_P(PSTR("<div class='card' style='border-left:3px solid "));
        server.sendContent_P(resultOk ? PSTR("#15803d") : PSTR("#dc2626"));
        server.sendContent_P(PSTR(";margin-bottom:16px'><p style='font-weight:500;color:"));
        server.sendContent_P(resultOk ? PSTR("#15803d") : PSTR("#dc2626"));
        server.sendContent_P(PSTR("'>"));
        server.sendContent(result.c_str());
        server.sendContent_P(PSTR("</p></div>"));
    }

    server.sendContent_P(PSTR("<form method='POST'>"));

    // Hidden slot count and names for POST round-trip
    char countStr[8];
    snprintf(countStr, sizeof(countStr), "%d", (int)slotNames.size());
    server.sendContent_P(PSTR("<input type='hidden' name='count' value='"));
    server.sendContent(countStr);
    server.sendContent_P(PSTR("'>"));

    for (int i = 0; i < (int)slotNames.size(); i++)
    {
        char idx[8];
        snprintf(idx, sizeof(idx), "%d", i);
        server.sendContent_P(PSTR("<input type='hidden' name='n"));
        server.sendContent(idx);
        server.sendContent_P(PSTR("' value='"));
        server.sendContent(htmlEsc(slotNames[i].c_str()).c_str());
        server.sendContent_P(PSTR("'>"));
    }

    // Interval card
    server.sendContent_P(PSTR("<div class='card' style='margin-bottom:16px'>"
                               "<h3 style='font-size:.95rem;font-weight:600;margin-bottom:10px'>"
                               "Cycle interval</h3>"
                               "<div style='display:flex;align-items:center;gap:8px'>"
                               "<input name='interval' type='number' min='10' value='"));
    server.sendContent(interval.c_str());
    server.sendContent_P(PSTR("' style='width:80px;padding:6px 8px;border:1px solid #cbd5e1;"
                               "border-radius:6px;font-size:.9rem'>"
                               " <span style='color:#475569'>seconds between cycles</span>"
                               "</div></div>"));

    // Existing slots table
    if (!slotNames.empty())
    {
        server.sendContent_P(PSTR("<div class='card' style='margin-bottom:16px'>"
                                   "<p style='color:#64748b;font-size:.82rem;margin-bottom:10px'>"
                                   "Clear <b>Text</b> and save to delete a slot. "
                                   "Use <code>{}</code> for countdown days; "
                                   "<code>{key}</code> inserts any config value "
                                   "(e.g. <code>{hostname}</code>, <code>{location}</code>).</p>"
                                   "<div style='overflow-x:auto'>"
                                   "<table style='table-layout:fixed;min-width:700px'>"
                                   "<colgroup><col style='width:110px'><col><col style='width:160px'>"
                                   "<col style='width:100px'><col style='width:100px'>"
                                   "<col style='width:100px'></colgroup>"
                                   "<tr><th>Name</th><th>Template</th><th>Preview</th>"
                                   "<th title='Show from'>Start</th>"
                                   "<th title='Hide after'>End</th>"
                                   "<th title='Countdown target'>Countdown</th></tr>\n"));

        for (int i = 0; i < (int)slotNames.size(); i++)
        {
            const std::string& name = slotNames[i];
            std::string pfx  = "message_" + name;
            std::string rawText = ds.get_value(pfx + "_text",      "");
            String text  = htmlEsc(rawText.c_str());
            String start =         ds.get_value(pfx + "_start",     "").c_str();
            String end   =         ds.get_value(pfx + "_end",       "").c_str();
            String cntdn =         ds.get_value(pfx + "_countdown", "").c_str();
            char   idx[8];
            snprintf(idx, sizeof(idx), "%d", i);

            CustomMessage m;
            m.text      = rawText;
            m.start     = parse_date(start.c_str());
            m.end       = parse_date(end.c_str());
            m.countdown = parse_date(cntdn.c_str());
            String preview = htmlEsc(build_display(m).c_str());

            server.sendContent_P(PSTR("<tr><td class='sn'>"));
            server.sendContent(htmlEsc(name.c_str()).c_str());
            server.sendContent_P(PSTR("</td><td><input class='mt' type='text' name='t"));
            server.sendContent(idx);
            server.sendContent_P(PSTR("' value='"));
            server.sendContent(text.c_str());
            server.sendContent_P(PSTR("' maxlength='128'></td>"));

            server.sendContent_P(PSTR("<td style='font-size:.82rem;color:#475569;word-break:break-word'>"));
            server.sendContent(preview.c_str());
            server.sendContent_P(PSTR("</td>"));

            server.sendContent_P(PSTR("<td><input class='md' type='date' name='s"));
            server.sendContent(idx);
            server.sendContent_P(PSTR("' value='"));
            if (start.length()) server.sendContent(start.c_str());
            server.sendContent_P(PSTR("'></td>"));

            server.sendContent_P(PSTR("<td><input class='md' type='date' name='e"));
            server.sendContent(idx);
            server.sendContent_P(PSTR("' value='"));
            if (end.length()) server.sendContent(end.c_str());
            server.sendContent_P(PSTR("'></td>"));

            server.sendContent_P(PSTR("<td><input class='md' type='date' name='c"));
            server.sendContent(idx);
            server.sendContent_P(PSTR("' value='"));
            if (cntdn.length()) server.sendContent(cntdn.c_str());
            server.sendContent_P(PSTR("'></td></tr>\n"));
        }
        server.sendContent_P(PSTR("</table></div></div>"));
    }

    // Add new slot card
    server.sendContent_P(PSTR("<div class='card'>"
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
                               "</div></div></div>"
                               "<div class='actions'>"
                               "<button class='btn btn-primary' type='submit'>&#128190; Save</button>"
                               "</div></form>"));
    sendPageFoot();
}
