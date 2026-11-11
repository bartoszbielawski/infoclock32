// LHC status from CERN's public LHC Page 1 (webfd.app.cern.ch).
//
// The /history endpoint takes ?channels= and answers plain JSON unless the
// client asks for gzip; status + comments is ~600 bytes. The latest entry of
// each channel is the last element of its array.

#include <WiFi.h>
#include <ArduinoJson.h>
#include <http_utils.hpp>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <logger.hpp>
#include <lhc_text.hpp>
#include <scroll_timing.hpp>
#include <string>

static const char kPageUrl[] =
    "https://webfd.app.cern.ch/lhc-vistar-page1/history?channels=status,comments";

static constexpr int kPollSeconds = 60;
static constexpr int kScrollMs = 20;   // ms per pixel step

// One scroll on its own display hold, with a slice sized to finish it
// (scroll_timing.hpp): an operator comment easily outlasts the shared
// display_min_hold_s slice, and a waiting clock would cut its tail.
static void scrollOnce(const std::string& message)
{
    auto& rmd = ResourceManager<LMDS>::getInstance();
    const uint32_t sliceMs = scroll_slice_ms(
        scroll_duration_ms(message.size(), kScrollMs), display_min_hold_ms());
    // Blocking acquire: cover the worst-case wait, then re-anchor the
    // watchdog window once granted (clock-task pattern).
    task_heartbeat_grace(90000);
    if (auto display = rmd.acquire())
    {
        task_heartbeat();
        task_heartbeat_grace(sliceMs + 30000);
        scrollMessage(message, display, kScrollMs, 1, sliceMs);
    }
}

struct LhcState
{
    std::string status;   // "SHUTDOWN: NO BEAM"
    std::string comment;  // operator comment, HTML stripped
};

// Fetches and parses the page; false (and `out` untouched) on any failure.
static bool fetchLhc(LhcState& out, int& httpCode)
{
    JsonDocument filter;
    JsonObject st = filter["channels"]["status"].add<JsonObject>();
    st["mode_disp"] = true;
    st["state_disp"] = true;
    st["energy_gev"] = true;
    filter["channels"]["comments"].add<JsonObject>()["comment"] = true;

    JsonDocument doc;
    DeserializationError err = DeserializationError::EmptyInput;
    httpCode = HttpUtils::httpGetStream(kPageUrl, [&](Stream& body) {
        err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    }, true, {{"Accept", "application/json"}});

    if (httpCode != 200)
        return false;
    if (err) {
        logPrintf("LHC", "JSON error: %s", err.c_str());
        return false;
    }

    JsonArray statuses = doc["channels"]["status"];
    if (statuses.isNull() || statuses.size() == 0) {
        logPrintf("LHC", "no status in the response");
        return false;
    }
    JsonObject s = statuses[statuses.size() - 1];
    std::string line = lhcStatusLine(s["mode_disp"] | "", s["state_disp"] | "",
                                     s["energy_gev"] | 0.0);
    if (line.empty()) {
        logPrintf("LHC", "status without a machine mode");
        return false;
    }

    std::string comment;
    JsonArray comments = doc["channels"]["comments"];
    if (!comments.isNull() && comments.size() > 0)
        comment = lhcHtmlToText(comments[comments.size() - 1]["comment"] | "");

    out.status = line;
    out.comment = comment;
    return true;
}

void lhc_status_task(void *parameter)
{
    // 60 s+ of work between beats is normal (poll + two scroll phases with
    // delays), plus display contention can stack on top
    registerTask("LHC", 8192, 120000);
    LhcState state;
    time_t last_update = 0;

    while (true)
    {
        task_heartbeat();
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        if (last_update == 0 || difftime(time(nullptr), last_update) > kPollSeconds)
        {
            LhcState fresh;
            int code = 0;
            if (fetchLhc(fresh, code))
            {
                if (fresh.status != state.status)
                    logPrintf("LHC", "status: %s", fresh.status.c_str());
                if (fresh.comment != state.comment)
                    logPrintf("LHC", "comment: %s", fresh.comment.c_str());
                state = fresh;
                last_update = time(nullptr);
            }
            else
            {
                if (code != 200)
                    logPrintf("LHC", "HTTP GET failed, response: %d", code);
                task_heartbeat_grace(320000);                // planned 5-minute cooldown below
                vTaskDelay(300 * 1000 / portTICK_PERIOD_MS); // keep the last good data meanwhile
                continue;
            }
        }

        if (!state.status.empty())
            scrollOnce("LHC " + state.status);

        vTaskDelay(10000 / portTICK_PERIOD_MS);

        if (!state.comment.empty())
            scrollOnce(state.comment);

        vTaskDelay(20000 / portTICK_PERIOD_MS);
    }
}
