#include "network.h"
#include "data.h"
#include "secrets.h"
#include <WiFi.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>

// ── Globals ───────────────────────────────────────────────────────────────────
static WiFiUDP _ntpUdp;
NTPClient ntp(_ntpUdp, "pool.ntp.org", UTC_OFFSET_SEC);
WebServer server(80);
bool      refreshRequested = false;

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static void onRoot() {
    char body[512];
    snprintf(body, sizeof(body), R"HTML(<!DOCTYPE html>
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
</body></html>)HTML", WiFi.localIP().toString().c_str());
    server.send(200, "text/html", body);
}

static void onRefresh() {
    refreshRequested = true;
    server.send(200, "application/json", R"({"ok":true})");
}

// ── Public ────────────────────────────────────────────────────────────────────
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
        ntp.begin();
        ntp.forceUpdate();
        fetchWeather();
        fetchMarkets();
    }
}

void setupServer() {
    server.on("/",        HTTP_GET,  onRoot);
    server.on("/refresh", HTTP_POST, onRefresh);
    server.begin();
}
