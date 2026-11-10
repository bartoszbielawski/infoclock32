#pragma once
// Shared web-UI infrastructure: WebServer instance, streaming layout helpers,
// and auth.  Include this in every HTTP handler .cpp file.

#include <WebServer.h>
#include <string>

// The single WebServer instance, defined in web_ui.cpp.
extern WebServer server;

WebServer* getWebServer();

// ── Streaming page-layout helpers ─────────────────────────────────────────────
// sendPageHead() opens a chunked HTTP response (setContentLength + send 200),
// then streams the <head> block (including shared CSS) and the site header bar.
// All subsequent sendContent*() calls append to that same response body.
void sendPageHead(const char* title, const char* extraHead = "");
void sendPageNav(const char* active);
void sendPageFoot();
// sendRow() escapes `value` (it is often device or config data);
// sendRowHtml() is for trusted markup built from string literals only.
void sendRow(const char* label, const char* value);
void sendRowHtml(const char* label, const char* html);

// Every dynamic value written into a page goes through this: config values
// can come from MQTT /config, runtime values from the network. Escapes
// & < > " and ' (attributes here are single-quoted); see html_utils.hpp.
String htmlEscaped(const char* s);
inline String htmlEscaped(const String& s) { return htmlEscaped(s.c_str()); }
inline String htmlEscaped(const std::string& s) { return htmlEscaped(s.c_str()); }
// Escape and send in one step.
void sendEscaped(const char* s);
inline void sendEscaped(const String& s) { sendEscaped(s.c_str()); }
inline void sendEscaped(const std::string& s) { sendEscaped(s.c_str()); }

// ── CSS endpoint ──────────────────────────────────────────────────────────────
// Serves the shared stylesheet with a long-lived cache header.
void handle_style_css();

// ── Status helpers ────────────────────────────────────────────────────────────
// Populate character buffers for status display (uptime, heap, RSSI).
void getStatusFields(char* uptime, size_t uptime_sz,
                     char* heap, size_t heap_sz,
                     char* rssi, size_t rssi_sz);

// ── Auth helpers ──────────────────────────────────────────────────────────────
// check_auth_header() → true when no password is configured, or the submitted
//                        password matches the "web_password" DataStore entry.
// is_authenticated()  → same check; sends 401 and returns false on failure.
bool check_auth_header();
bool is_authenticated();
// request_same_origin() -> false when a browser's Origin/Referer names a host
//                          other than the one the request was sent to (CSRF).
bool request_same_origin();
