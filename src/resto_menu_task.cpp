/*
 * resto_menu_task.cpp
 *
 * Converted to a single FreeRTOS task (ESP32). No web page support.
 *
 * Created on: 27.07.2025 (original)
 * Converted by: GitHub Copilot
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <vector>
#include <set>
#include <map>
#include "time.h"

// Adjust as needed
#define MAX_DISHES 10
#define MENU_FETCH_INTERVAL_MS (900 * 1000UL) // 900 seconds = 15 minutes
#define DISPLAY_PERIOD_MS 25U

#define DEFAULT_MENU_START_HOUR 9
#define DEFAULT_MENU_END_HOUR 14

namespace RMenu {

static const struct { int code; const char* id; } restaurants[] = {
    {1, "13-restaurant-r1"},
    {2, "21-restaurant-r2"},
    {3, "33-restaurant-r3"}
};
static constexpr size_t NUM_RESTAURANTS = sizeof(restaurants) / sizeof(restaurants[0]);

inline String codeToId(int code) {
    for (size_t i = 0; i < NUM_RESTAURANTS; ++i)
        if (restaurants[i].code == code)
            return restaurants[i].id;
    return restaurants[0].id;
}
inline int codeSanitize(int code) {
    for (size_t i = 0; i < NUM_RESTAURANTS; ++i)
        if (restaurants[i].code == code)
            return code;
    return restaurants[0].code;
}

String normalizeFrenchText(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0xC3) { out += in[i]; continue; }
        if (c == 0xC3 && i+1 < in.length()) {
            unsigned char d = (unsigned char)in[i+1];
            switch (d) {
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: out += 'a'; break;
                case 0xA7: out += 'c'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: out += 'e'; break;
                case 0xAC: case 0xAD: case 0xAE: case 0xAF: out += 'i'; break;
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: out += 'o'; break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC: out += 'u'; break;
                case 0xBF: out += 'y'; break;
                case 0x80: case 0x82: case 0x83: case 0x84: case 0x85: out += 'A'; break;
                case 0x87: out += 'C'; break;
                case 0x88: case 0x89: case 0x8A: case 0x8B: out += 'E'; break;
                case 0x8C: case 0x8D: case 0x8E: case 0x8F: out += 'I'; break;
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: out += 'O'; break;
                case 0x99: case 0x9A: case 0x9B: case 0x9C: out += 'U'; break;
                case 0x9F: out += 'Y'; break;
                default: out += in[i]; out += in[i+1];
            }
            i++; continue;
        }
        if (c == 0xC5 && i+1 < in.length()) {
            unsigned char d = (unsigned char)in[i+1];
            if (d == 0x92) { out += "OE"; i++; continue; }
            if (d == 0x93) { out += "oe"; i++; continue; }
        }
        if (c == 0xE2 && i + 2 < in.length()) {
            if ((unsigned char)in[i+1] == 0x80 && (unsigned char)in[i+2] == 0x99) {
                out += "'"; i += 2; continue;
            }
        }
        out += in[i];
    }
    return out;
}

static String trimmedKeyWords(const String& dish, int maxWords = 4) {
    const char* stopwords[] = { "aux","de","et","avec","à","le","la","du","des","en","au","sur","pour","les","un","une","deux","trois","quatre","d'","l'","with","and","of","in","for","the","to","on","at","from","by","an","a","one","two","three","four", "fresh", "old fashioned", "organic", "mature", "traditional", "natural", "style", "sliced", "drenched"};
    const size_t nStops = sizeof(stopwords) / sizeof(stopwords[0]);
    String out; int found = 0; size_t start = 0;
    while (found < maxWords && start < dish.length()) {
        size_t end = dish.indexOf(' ', start);
        if (end == (size_t)-1) end = dish.length();
        String word = dish.substring(start, end);
        word.trim();
        word.replace(",", ""); word.replace(".", ""); word.replace(";", "");
        word.replace("/", " "); word.replace("&", ""); word.replace(":", "");
        word.replace("-", " "); word.replace("(", ""); word.replace(")", "");
        word.replace("+", ""); word.replace("[", ""); word.replace("]", "");
        word.replace("*", ""); word.replace("$", ""); word.replace("#", "");
        word.replace("{", ""); word.replace("}", ""); word.replace("@", "");
        bool isStop = false;
        for (size_t j = 0; j < nStops; ++j)
            if (word.equalsIgnoreCase(stopwords[j])) { isStop = true; break; }
        if (!isStop && word.length() > 0) { if (!out.isEmpty()) out += ' '; out += word; found++; }
        start = end + 1;
    }
    return out;
}

// Shared state protected by mutex
static SemaphoreHandle_t menuMutex = nullptr;
static String cachedMenuLine;
static String cachedMenuDate;
static String lastStatusTimestamp;
static int restaurantCode = 3;
static String restaurantId = codeToId(restaurantCode);
static int menuStartHour = DEFAULT_MENU_START_HOUR;
static int menuEndHour = DEFAULT_MENU_END_HOUR;
static bool menuShowTomorrow = false;

// transient
static std::vector<String> dishes;
static int lastFetchHour = -1;
static String lastFetchedMenuDate;

String makeMenuDateString(time_t base) {
    char buf[11];
    struct tm t;
    localtime_r(&base, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return String(buf);
}

void updateMenuHoursFromConfig() {
    int startHour = readConfigWithDefault(F("menuStartHour"), String(DEFAULT_MENU_START_HOUR).c_str()).toInt();
    if (startHour >= 0 && startHour <= 23) menuStartHour = startHour;
    else menuStartHour = DEFAULT_MENU_START_HOUR;

    int endHour = readConfigWithDefault(F("menuEndHour"), String(DEFAULT_MENU_END_HOUR).c_str()).toInt();
    if (endHour >= 0 && endHour <= 23) menuEndHour = endHour;
    else menuEndHour = DEFAULT_MENU_END_HOUR;
}

bool isWithinDisplayHour() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int hour = t.tm_hour;
    if (menuStartHour < menuEndHour)
        return hour >= menuStartHour && hour < menuEndHour;
    else
        return hour >= menuStartHour || hour < menuEndHour;
}

String getMenuString() {
    // returns empty string when no menu should be displayed
    // copy under mutex
    if (!menuMutex) return String();

    xSemaphoreTake(menuMutex, portMAX_DELAY);
    String localCachedLine = cachedMenuLine;
    String localCachedDate = cachedMenuDate;
    int localMenuStart = menuStartHour;
    int localMenuEnd = menuEndHour;
    bool localShowTomorrow = menuShowTomorrow;
    int localRestaurantCode = restaurantCode;
    xSemaphoreGive(menuMutex);

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int hour = t.tm_hour;

    bool afterEnd;
    if (localMenuStart < localMenuEnd)
        afterEnd = (hour >= localMenuEnd);
    else
        afterEnd = (hour >= localMenuEnd && hour < localMenuStart);

    bool withinWindow = ( (localMenuStart < localMenuEnd) ? (hour >= localMenuStart && hour < localMenuEnd)
                                                         : (hour >= localMenuStart || hour < localMenuEnd) );

    bool showTomorrow = false;
    if (withinWindow) {
        // nothing
    } else if (afterEnd && localShowTomorrow) {
        now += 24 * 60 * 60;
        showTomorrow = true;
    } else {
        return String();
    }

    String wantedDate = makeMenuDateString(now);
    if (localCachedDate != wantedDate || localCachedLine.isEmpty())
        return String();

    String labelPrefix = showTomorrow ? "Tomorrow's " : "Today's ";
    String menuPrefix = "R" + String(localRestaurantCode) + " menu: ";
    return labelPrefix + menuPrefix + localCachedLine;
}

void fetchMenu(const String& dateStr) {
    logPrintfX(F("RMT"), F("Starting fetchMenu for date: %s restaurant: %s"), dateStr.c_str(), restaurantId.c_str());

    dishes.clear();
    std::set<String> seen;
    String url = "https://api.mynovae.ch/en/api/v2/salepoints/" + restaurantId + "/menus/" + dateStr;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) {
        logPrintfX(F("RMT"), F("HTTP begin failed"));
        return;
    }
    http.addHeader("Novae-Codes", novaeKey);
    http.addHeader("Accept", "application/json");
    http.addHeader("X-Requested-With", "xmlhttprequest");

    int httpCode = -1;
    int attempts = 3;
    while (attempts-- && (httpCode == -1)) {
        httpCode = http.GET();
        if (httpCode == -1) {
            logPrintfX(F("RMT"), F("Fetching Menu returned HTTP Error %d"), httpCode);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    if (httpCode == 200) {
        WiFiClient* stream = http.getStreamPtr();
        // skip whitespace and initial '['
        while (stream->available()) {
            char c = stream->peek();
            if (c == '[') { stream->read(); break; }
            if (isspace((unsigned char)c)) stream->read(); else break;
        }
        while (stream->available()) {
            char c = stream->peek();
            if (isspace((unsigned char)c) || c == ',') { stream->read(); continue; }
            if (c == ']') { stream->read(); break; }
            if (c != '{') { stream->read(); continue; }

            StaticJsonDocument<768> doc;
            StaticJsonDocument<64> filter;
            filter["title"] = true;
            filter["model"]["service"] = true;

            DeserializationError err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
            if (!err) {
                const char* service = doc["model"]["service"];
                if (!service) continue;
                String serviceStr(service);
                serviceStr.toLowerCase();
                if (serviceStr != "midi") continue;

                JsonObject title = doc["title"];
                String dish;
                if (title.containsKey("en") && strlen(title["en"])) dish = String(title["en"].as<const char*>());
                else if (title.containsKey("fr") && strlen(title["fr"])) dish = String(title["fr"].as<const char*>());
                if (dish.length()) {
                    String tmp = trimmedKeyWords(normalizeFrenchText(dish), 4);
                    if (tmp.length() && seen.find(tmp) == seen.end()) {
                        seen.insert(tmp);
                        dishes.push_back(tmp);
                        logPrintfX(F("RMT"), F("Added dish: %s"), tmp.c_str());
                        if (dishes.size() >= MAX_DISHES) break;
                    }
                }
            } else {
                // attempt to skip this malformed object
                int depth = 0;
                while (stream->available()) {
                    char cc = stream->read();
                    if (cc == '{') depth++;
                    if (cc == '}') { if (depth == 0) break; depth--; }
                }
            }
        }

        logPrintfX(F("RMT"), F("Fetch menu completed"));

        xSemaphoreTake(menuMutex, portMAX_DELAY);
        if (dishes.empty()) {
            cachedMenuLine = "";
            logPrintfX(F("RMT"), F("No dishes found for date %s"), dateStr.c_str());
        } else {
            String allDishes;
            for (auto& dish : dishes) {
                if (!allDishes.isEmpty()) allDishes += " | ";
                allDishes += dish;
            }
            cachedMenuLine = allDishes;
            cachedMenuDate = dateStr;
        }
        lastStatusTimestamp = getDateTime();
        xSemaphoreGive(menuMutex);
    } else {
        logPrintfX(F("RMT"), F("HTTP GET failed with code %d, no menu fetched"), httpCode);
    }
    http.end();
}

void restaurantMenuTask(void* pvParameters) {
    (void)pvParameters;
    menuMutex = xSemaphoreCreateMutex();
    if (!menuMutex) {
        logPrintfX(F("RMT"), F("Failed to create mutex"));
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        updateMenuHoursFromConfig();

        int code = readConfigWithDefault(F("restaurant"), "3").toInt();
        restaurantCode = codeSanitize(code);
        restaurantId = codeToId(restaurantCode);

        menuShowTomorrow = readConfigWithDefault(F("menuShowTomorrow"), "0").toInt() == 1;

        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        int hour = tm_now.tm_hour;

        bool afterEnd;
        if (menuStartHour < menuEndHour) afterEnd = (hour >= menuEndHour);
        else afterEnd = (hour >= menuEndHour && hour < menuStartHour);

        time_t activeTime = now;
        if (afterEnd && menuShowTomorrow) activeTime += 24 * 60 * 60;

        String activeMenuDate = makeMenuDateString(activeTime);

        bool menuBoundary = (lastFetchedMenuDate != activeMenuDate);
        bool hourBoundary = (lastFetchHour != hour);
        if (menuBoundary || hourBoundary) {
            lastFetchedMenuDate = activeMenuDate;
            lastFetchHour = hour;
            fetchMenu(activeMenuDate);
        }

        vTaskDelay(pdMS_TO_TICKS(MENU_FETCH_INTERVAL_MS));
    }
}

// start helper
TaskHandle_t startRestaurantMenuTask(UBaseType_t priority = tskIDLE_PRIORITY + 1, uint32_t stackSize = 8192) {
    TaskHandle_t handle = nullptr;
    xTaskCreate(restaurantMenuTask, "RestaurantMenu", stackSize / sizeof(StackType_t), nullptr, priority, &handle);
    return handle;
}

} // namespace RMenu
