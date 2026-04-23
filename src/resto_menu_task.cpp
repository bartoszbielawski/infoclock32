/*
 * resto_menu_task.cpp
 *
 * Fetches lunch (midi) menus from api.mynovae.ch for one or more configured
 * CERN restaurants and scrolls them during a configurable time window.
 *
 * Config keys (set via /edit or MQTT /config):
 *   resto_restaurants  – comma-separated restaurant numbers, e.g. "2,3" (default "3")
 *   resto_start_hour   – first hour to display menu, 0-23 (default 9)
 *   resto_end_hour     – last hour (exclusive) to display menu, 0-23 (default 14)
 *   novae_codes        – Novae API group code, e.g. "CER103" (default "CER103")
 */

#include <WiFi.h>
#include <http_utils.hpp>
#include <resource_manager.hpp>
#include <task_registry.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>
#include <string_utils.h>

#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <set>

// ── constants ────────────────────────────────────────────────────────────────

static const char TAG[] = "RST";

static const struct { int code; const char* id; } kRestaurants[] = {
    {1, "13-restaurant-r1"},
    {2, "21-restaurant-r2"},
    {3, "33-restaurant-r3"},
};
static constexpr size_t kNumRestaurants = sizeof(kRestaurants) / sizeof(kRestaurants[0]);

static const int kDefaultStartHour  = 9;
static const int kDefaultEndHour    = 14;
static const uint32_t kFetchIntervalMs = 60UL * 60UL * 1000UL; // 1 hour
static const uint32_t kScrollSpeedMs  = 10;

// ── helpers ──────────────────────────────────────────────────────────────────

static const char* codeToId(int code) {
    for (size_t i = 0; i < kNumRestaurants; ++i)
        if (kRestaurants[i].code == code) return kRestaurants[i].id;
    return kRestaurants[0].id;
}

// Strip everything from the first newline onward (garnish/side-dish annotations).
static std::string stripSuffix(const std::string& s) {
    auto pos = s.find('\n');
    return pos != std::string::npos ? s.substr(0, pos) : s;
}

// Parse a comma-separated string of integers.
static std::vector<int> parseIntList(const std::string& s) {
    std::vector<int> result;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        int v = atoi(s.substr(start, end - start).c_str());
        if (v > 0) result.push_back(v);
        start = end + 1;
    }
    return result;
}

// Build YYYY-MM-DD string from a time_t.
static std::string dateString(time_t t) {
    char buf[11];
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

// Returns true if the current hour falls within [startHour, endHour).
static bool withinWindow(int startHour, int endHour) {
    struct tm tm_buf;
    time_t now = time(nullptr);
    localtime_r(&now, &tm_buf);
    int h = tm_buf.tm_hour;
    if (startHour < endHour) return h >= startHour && h < endHour;
    return h >= startHour || h < endHour; // wraps midnight
}

// ── fetch ────────────────────────────────────────────────────────────────────

// Fetch and return deduplicated dish titles for one restaurant on a given date.
static std::string fetchMenu(int restaurantCode, const std::string& dateStr,
                             const std::string& lang) {
    const char* restaurantId = codeToId(restaurantCode);
    // API path language: "en" or "fr" (no Polish menu available).
    const char* apiLang = (lang == "fr") ? "fr" : "en";
    char url[128];
    snprintf(url, sizeof(url),
             "https://api.mynovae.ch/%s/api/v2/salepoints/%s/menus/%s",
             apiLang, restaurantId, dateStr.c_str());

    // novae_codes identifies your CERN group to the Novae API.
    // Set it via /edit or MQTT /config if the default is wrong.
    String novaeCode = DataStore::getInstance().get_value("novae_codes", "CER103").c_str();

    String body;
    int code = HttpUtils::httpGet(url, body, true, {
        {"Novae-Codes",      novaeCode},
        {"Accept",           "application/json"},
        {"X-Requested-With", "xmlhttprequest"},
    });

    if (code != 200) {
        logPrintf(TAG, "R%d HTTP %d", restaurantCode, code);
        return {};
    }

    // Parse the JSON array, filtering for midi service only.
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        logPrintf(TAG, "R%d JSON error: %s", restaurantCode, err.c_str());
        return {};
    }

    std::set<std::string> seen;
    std::vector<std::string> dishes;

    for (JsonObject item : doc.as<JsonArray>()) {
        const char* service = item["model"]["service"];
        if (!service || strcmp(service, "midi") != 0) continue;

        JsonObject title = item["title"];
        // Prefer the configured language, fall back to the other.
        const char* pref = (lang == "fr") ? "fr" : "en";
        const char* fall = (lang == "fr") ? "en" : "fr";
        const char* raw = nullptr;
        if (title.containsKey(pref) && title[pref].as<const char*>() && strlen(title[pref]))
            raw = title[pref];
        else if (title.containsKey(fall) && title[fall].as<const char*>() && strlen(title[fall]))
            raw = title[fall];
        if (!raw) continue;

        std::string dish = stripSuffix(normalizeFrench(raw));

        if (dish.empty() || seen.count(dish)) continue;
        seen.insert(dish);
        dishes.push_back(dish);
        logPrintf(TAG, "R%d: %s", restaurantCode, dish.c_str());
    }

    if (dishes.empty()) {
        logPrintf(TAG, "R%d: no midi dishes for %s", restaurantCode, dateStr.c_str());
        return {};
    }

    std::string out = "R" + std::to_string(restaurantCode) + ":";
    for (const auto& d : dishes) {
        out += " ";
        out += d;
        out += " |";
    }
    // remove trailing " |"
    if (out.size() >= 2) out.resize(out.size() - 2);
    return out;
}

// ── task ─────────────────────────────────────────────────────────────────────

void resto_menu_task(void* pvParameters) {
    registerTask("RestoMenu", 8192);
    (void)pvParameters;

    auto& rmd = ResourceManager<LMDS>::getInstance();

    std::vector<std::string> cachedMenus; // one entry per configured restaurant
    std::string cachedDate;
    time_t lastFetch = 0;

    while (true) {
        // ── config ────────────────────────────────────────────────────────
        auto& ds = DataStore::getInstance();
        int startHour = ds.get_value<int>("resto_start_hour", kDefaultStartHour);
        int endHour   = ds.get_value<int>("resto_end_hour",   kDefaultEndHour);
        if (startHour < 0 || startHour > 23) startHour = kDefaultStartHour;
        if (endHour   < 0 || endHour   > 23) endHour   = kDefaultEndHour;

        std::vector<int> codes = parseIntList(
            ds.get_value("resto_restaurants", "3"));
        if (codes.empty()) codes.push_back(3);

        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(30000 / portTICK_PERIOD_MS);
            continue;
        }

        // ── fetch if needed ───────────────────────────────────────────────
        // resto_test_date (YYYY-MM-DD) overrides fetch date and bypasses the
        // time window so you can test on weekends or outside lunch hours.
        std::string testDate = ds.get_value("resto_test_date", "");
        bool testing = !testDate.empty();
        std::string fetchDate = testing ? testDate : dateString(time(nullptr));

        auto currentTime = time(nullptr);

        bool stale = (fetchDate != cachedDate) ||
                     (difftime(currentTime, lastFetch) > kFetchIntervalMs / 1000.0) && (!currentTime < 3600); // sanity check to avoid treating an invalid clock as stale

        if (stale) {
            cachedDate  = fetchDate;
            lastFetch   = time(nullptr);
            cachedMenus.clear();
            std::string lang = ds.get_value("language", "en");
            for (int code : codes) {
                std::string menu = fetchMenu(code, fetchDate, lang);
                if (!menu.empty()) cachedMenus.push_back(menu);
            }
        }

        // ── display ───────────────────────────────────────────────────────
        if ((!testing && !withinWindow(startHour, endHour)) || cachedMenus.empty()) {
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }

        for (const auto& menu : cachedMenus) {
            if (auto display = rmd.acquire())
                scrollMessage(menu, display, kScrollSpeedMs);
            else
            {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
            
            //this delay is to avoid scrolling multiple menus back-to-back without giving a chance for other tasks to show their messages in between; adjust as needed
            vTaskDelay(120 * 1000 / portTICK_PERIOD_MS);
        }
    }
}
