#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <functional>
#include <map>
#include <string>

// ── Data types ──────────────────────────────────────────────────────

/// Identifies a single WRAP/JAPC parameter.
struct WrapParam {
    std::string key;        ///< User-defined identifier — echoed in every callback.
    std::string device;
    std::string property;
    std::string field;
    std::string selector;   ///< Timing selector; "" = no selector (broadcast).
};

/// Delivered to the callback for every incoming message.
/// type == 'V' or 'C' : values is populated, error is empty.
/// type == 'E'        : error is populated, values is a null variant.
///
/// IMPORTANT: values is a view into an internal JsonDocument.
///            It is only valid during the callback — do not store it.
struct WrapMsg {
    std::string      key;          ///< Matches WrapParam::key.
    std::string      accessPoint;  ///< "DEVICE/PROPERTY" as sent by the server.
    std::string      selector;
    unsigned long    timestamp;
    char             type;         ///< 'V'=value, 'C'=cycle-bound, 'E'=error.
    JsonVariantConst values;       ///< Server values object. Valid only during the callback!
    std::string      error;        ///< Non-empty when type == 'E'.
};

using WrapCb = std::function<void(const WrapMsg&)>;

// ── WrapClient ──────────────────────────────────────────────────────

/// WebSocket client for the WRAP/JAPC real-time data API.
///
/// All subscriptions share a single callback supplied at construction.
/// Call loop() frequently from a FreeRTOS task; callbacks fire on the
/// same task that calls loop().
///
/// Example:
///   WrapClient wrap(
///       "wrap-staging.cern.ch", 443, "https://wrap-staging.cern.ch",
///       [](const WrapMsg& msg) {
///           if (msg.type == 'E') {
///               logPrintf("WRAP", "Error [%s]: %s",
///                         msg.key.c_str(), msg.error.c_str());
///               return;
///           }
///           RuntimeStore::getInstance().set(
///               msg.key, msg.values["value"].as<std::string>());
///       });
///
///   wrap.subscribe({"lhc.mode", "LHC.BeamMode", "Acquisition", "value", ""});
///
///   // in task loop:
///   for (;;) { wrap.loop(); vTaskDelay(pdMS_TO_TICKS(10)); }
class WrapClient {
public:
    /// @param wsHost    WebSocket host (e.g. "wrap-staging.cern.ch").
    /// @param port      Port — 443 for WSS.
    /// @param rdaBase   Base URL for HTTP set calls
    ///                  (e.g. "https://wrap-staging.cern.ch").
    ///                  Leave empty to disable set().
    /// @param callback  Universal callback for all subscribed parameters.
    explicit WrapClient(const std::string& wsHost    = "wrap-staging.cern.ch",
                        uint16_t           port      = 443,
                        const std::string& rdaBase   = "",
                        WrapCb             callback  = nullptr);

    /// Start receiving updates for param. No-op if already subscribed.
    void subscribe(const WrapParam& param);

    /// Stop receiving updates for param. No-op if not subscribed.
    void unsubscribe(const WrapParam& param);

    /// Set a parameter value via HTTP PUT to {rdaBase}/api/japc.
    /// T may be any ArduinoJson-serialisable type:
    ///   int, float, double, bool, const char*, std::string.
    /// Returns false if rdaBase is empty or the request fails.
    template<typename T>
    bool set(const WrapParam& param, T value);

    /// Drive the WebSocket engine — call this in the owning task loop.
    void loop();

    /// Permanently close the connection (no reconnect).
    void close();

    bool connected();

private:
    struct Sub { WrapParam param; };

    std::string       host_;
    uint16_t          port_;
    std::string       rdaBase_;
    WrapCb            callback_;
    WebSocketsClient  ws_;
    std::map<std::string, Sub> subs_;  // key → Sub
    DynamicJsonDocument rxDoc_;
    bool              closed_;

    void connect_();
    void onWsEvent_(WStype_t type, uint8_t* payload, size_t length);
    void sendSub_(const WrapParam& param, bool subscribe);
    void resubscribeAll_();
    void dispatch_(const uint8_t* payload, size_t length);
    bool httpPut_(const String& body);
};

// ── Template implementation (must live in header) ───────────────────

template<typename T>
bool WrapClient::set(const WrapParam& param, T value) {
    if (rdaBase_.empty()) return false;

    DynamicJsonDocument doc(512);
    JsonArray arr = doc.to<JsonArray>();
    JsonObject obj = arr.createNestedObject();
    obj["device"]   = param.device;
    obj["property"] = param.property;
    obj["field"]    = param.field;
    obj["selector"] = param.selector;
    obj["value"]    = value;

    String body;
    serializeJson(doc, body);
    return httpPut_(body);
}
