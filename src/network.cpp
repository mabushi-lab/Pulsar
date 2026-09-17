#include "network.h"
#include "config.h"
#include "secrets.h"
#include <WiFi.h>
#include <time.h>

// ── Globals ───────────────────────────────────────────────────────────────────
WebServer server(80);
bool      refreshRequested = false;

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static void onRoot() {
    // The buffer is sized from the template itself, so editing the markup can
    // never silently truncate the page again. "%s" expands to at most 15 chars
    // ("255.255.255.255"); +32 leaves headroom for the NUL and future edits.
    static const char PAGE_FMT[] = R"HTML(<!DOCTYPE html>
<html><head><title>Pulsar</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#06080F;color:#EEF2FF;font-family:monospace;max-width:420px;margin:40px auto;padding:20px}
h2{color:#33CCFF}p{color:#7A93AC;font-size:13px}
button{padding:10px 20px;background:#0D1B2A;border:1px solid #33CCFF;color:#33CCFF;
border-radius:4px;cursor:pointer;font-size:14px}
button:hover{filter:brightness(1.3)}</style></head>
<body><h2>Pulsar Market Dashboard</h2>
<p>Device IP: %s</p>
<button onclick="fetch('/refresh',{method:'POST'}).then(()=>alert('Refreshing...'))">
Refresh Markets Now</button>
</body></html>)HTML";

    char body[sizeof(PAGE_FMT) + 32];
    snprintf(body, sizeof(body), PAGE_FMT, WiFi.localIP().toString().c_str());
    server.send(200, "text/html", body);
}

static void onRefresh() {
    refreshRequested = true;
    server.send(200, "application/json", R"({"ok":true})");
}

// ── Wi-Fi ─────────────────────────────────────────────────────────────────────
// Connecting no longer fetches data as a side effect: loop() owns scheduling,
// so a reconnect cannot smuggle six blocking HTTPS requests into a retry.
void wifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // board is USB powered; trade idle mA for latency
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

// Only used behind the boot splash, which already says "Connecting...".
bool wifiWaitConnected(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!wifiConnected() && millis() - start < timeoutMs) delay(100);
    return wifiConnected();
}

// Nudges a reconnect at most every WIFI_RETRY_MS and returns immediately, so an
// outage no longer freezes the display for 20 s out of every 30.
void wifiMaintain(uint32_t now) {
    static uint32_t lastRetry = 0;
    if (wifiConnected()) { lastRetry = now; return; }
    if (now - lastRetry < WIFI_RETRY_MS) return;
    lastRetry = now;
    WiFi.reconnect();
}

// ── Time ──────────────────────────────────────────────────────────────────────
void timeBegin() {
    configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
}

// SNTP starts the clock at the epoch, so anything before 2023 means no sync yet.
bool timeSynced() { return time(nullptr) > 1700000000; }

bool localNow(struct tm* out) {
    time_t nowSec = time(nullptr);
    if (nowSec <= 1700000000) return false;
    localtime_r(&nowSec, out);
    return true;
}

void setupServer() {
    server.on("/",        HTTP_GET,  onRoot);
    server.on("/refresh", HTTP_POST, onRefresh);
    server.begin();
}
