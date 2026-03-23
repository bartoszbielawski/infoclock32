// wrap_client.cpp — WRAP/JAPC WebSocket + HTTP client for ESP32.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#if __has_include(<WiFiClientSecure.h>)
  #include <WiFiClientSecure.h>
  #define HAS_WIFI_CLIENT_SECURE
#endif
#include <wrap_client.hpp>
#include <logger.hpp>

// ── Constructor ──────────────────────────────────────────────────────

WrapClient::WrapClient(const std::string& wsHost, uint16_t port,
                       const std::string& rdaBase, WrapCb callback)
    : host_(wsHost)
    , port_(port)
    , rdaBase_(rdaBase)
    , callback_(callback)
    , rxDoc_(4096)
    , closed_(false)
{
    connect_();
}

// ── Public API ───────────────────────────────────────────────────────

void WrapClient::subscribe(const WrapParam& param) {
    if (subs_.count(param.key)) return;
    subs_[param.key] = { param };
    sendSub_(param, true);
}

void WrapClient::unsubscribe(const WrapParam& param) {
    auto it = subs_.find(param.key);
    if (it == subs_.end()) return;
    sendSub_(it->second.param, false);
    subs_.erase(it);
}

void WrapClient::loop() {
    ws_.loop();
}

void WrapClient::close() {
    closed_ = true;
    ws_.disconnect();
}

bool WrapClient::connected() {
    return ws_.isConnected();
}

// ── Private ──────────────────────────────────────────────────────────

void WrapClient::connect_() {
    if (closed_) return;

    ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        onWsEvent_(type, payload, length);
    });

    ws_.beginSSL(host_.c_str(), port_, "/ws/data/");
    ws_.setReconnectInterval(2000);
    ws_.enableHeartbeat(15000, 3000, 2);
}

void WrapClient::onWsEvent_(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            logPrintf("WRAP", "Connected to %s", host_.c_str());
            resubscribeAll_();
            break;
        case WStype_DISCONNECTED:
            logPrintf("WRAP", "Disconnected");
            break;
        case WStype_TEXT:
            dispatch_(payload, length);
            break;
        case WStype_ERROR:
            logPrintf("WRAP", "WebSocket error");
            break;
        default:
            break;
    }
}

void WrapClient::sendSub_(const WrapParam& param, bool subscribe) {
    if (!ws_.isConnected()) return;

    DynamicJsonDocument doc(256);
    doc["type"] = subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE";
    JsonObject p = doc.createNestedObject("parameter");
    p["device"]   = param.device;
    p["property"] = param.property;
    p["field"]    = param.field;
    // Send null selector when empty — the server treats "" and null differently.
    if (param.selector.empty()) {
        p["selector"] = nullptr;
    } else {
        p["selector"] = param.selector;
    }
    doc["reference"] = false;

    String msg;
    serializeJson(doc, msg);
    ws_.sendTXT(msg);
}

void WrapClient::resubscribeAll_() {
    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        sendSub_(it->second.param, true);
    }
}

void WrapClient::dispatch_(const uint8_t* payload, size_t length) {
    rxDoc_.clear();
    DeserializationError err = deserializeJson(rxDoc_, payload, length);
    if (err) {
        logPrintf("WRAP", "JSON parse error: %s", err.c_str());
        return;
    }

    const char* ap = rxDoc_["accessPoint"];
    if (!ap) return;

    const char* typeStr = rxDoc_["type"] | "?";
    char type = typeStr[0];

    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        const WrapParam& p = it->second.param;
        // accessPoint from server is "DEVICE/PROPERTY"
        std::string expectedAp = p.device + "/" + p.property;
        if (expectedAp != ap) continue;

        WrapMsg msg;
        msg.key         = it->first;
        msg.accessPoint = ap;
        msg.selector    = rxDoc_["selector"] | "";
        msg.timestamp   = rxDoc_["timestamp"] | 0UL;
        msg.type        = type;

        if (type == 'E') {
            msg.error = rxDoc_["message"] | "";
        } else {
            msg.values = rxDoc_["values"];
        }

        if (callback_) callback_(msg);
    }
}

bool WrapClient::httpPut_(const String& body) {
    String url = String(rdaBase_.c_str()) + "/api/japc";

    HTTPClient http;
    WiFiClient plainClient;
    bool ok = false;

#ifdef HAS_WIFI_CLIENT_SECURE
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    if (url.startsWith("https")) {
        ok = http.begin(secureClient, url);
    } else {
        ok = http.begin(plainClient, url);
    }
#else
    if (url.startsWith("https")) {
        logPrintf("WRAP", "HTTPS not supported on this build (no WiFiClientSecure)");
        return false;
    }
    ok = http.begin(plainClient, url);
#endif

    if (!ok) {
        logPrintf("WRAP", "http.begin() failed for PUT %s", url.c_str());
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    int code = http.PUT(const_cast<String&>(body));
    http.end();

    if (code != 200) {
        logPrintf("WRAP", "PUT /api/japc -> %d", code);
        return false;
    }
    return true;
}
