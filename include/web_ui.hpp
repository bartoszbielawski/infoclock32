#pragma once
// Shared web-UI infrastructure: WebServer instance, streaming layout helpers,
// and auth.  Include this in every HTTP handler .cpp file.

#include <WebServer.h>

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
void sendRow(const char* label, const char* value);

// ── CSS endpoint ──────────────────────────────────────────────────────────────
// Serves the shared stylesheet with a long-lived cache header.
void handle_style_css();

// ── Auth helpers ──────────────────────────────────────────────────────────────
// check_auth_header() → true when no password is configured, or the submitted
//                        password matches the "web_password" DataStore entry.
// is_authenticated()  → same check; sends 401 and returns false on failure.
// invalidate_auth_cache() → force re-read of web_password from DataStore on
//                           the next request (call after /edit saves config).
bool check_auth_header();
bool is_authenticated();
void invalidate_auth_cache();
