#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "data.h"
#include "network.h"

// ── Buttons ───────────────────────────────────────────────────────────────────
static void checkButtons() {
    static bool     prevBoot   = HIGH, prevUser = HIGH;
    static bool     fullBright = true;
    static uint32_t lastMs     = 0;

    if (millis() - lastMs < 50) return;
    lastMs = millis();

    bool boot = digitalRead(BTN_BOOT);
    bool user = digitalRead(BTN_USER);

    if (prevBoot == HIGH && boot == LOW) {
        fullBright = !fullBright;
        displaySetBrightness(fullBright);
    }
    if (prevUser == HIGH && user == LOW) {
        refreshRequested = true;
    }
    prevBoot = boot;
    prevUser = user;
}

// ── Entry points ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);
    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_USER, INPUT_PULLUP);

    displayInit();
    drawSplash("Connecting...");

    connectWiFi();
    setupServer();
    drawAll();
}

void loop() {
    checkButtons();
    server.handleClient();

    static uint32_t lastSec     = 0;
    static uint32_t lastWeather = 0;
    static uint32_t lastMarkets = 0;
    static int      lastPct     = -1;
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    uint32_t now    = millis();
    bool     wifiOk = (WiFi.status() == WL_CONNECTED);

    // WiFi reconnect
    if (!wifiOk) {
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
            lastRetry = now;
            connectWiFi();
            if (WiFi.status() == WL_CONNECTED) { drawAll(); wifiWas = true; }
            else                                  drawHeader();
        }
        return;
    }
    if (!wifiWas) { wifiWas = true; drawAll(); }

    // Manual market refresh via USER button or POST /refresh
    if (refreshRequested) {
        refreshRequested = false;
        lastMarkets = now;
        fetchMarkets();
        drawAllMarkets();
        drawDividers();
    }

    // 1-second tick: header (once/min) + progress bar (when % changes)
    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();

        if (ntp.getSeconds() == 0) {
            drawHeader();
            drawDividers();
        }

        time_t     epoch = ntp.getEpochTime();
        struct tm *t     = localtime(&epoch);
        int        secs  = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
        int        pct   = (int)((long)secs * 100 / 86400);
        if (pct != lastPct) { lastPct = pct; drawProgress(); }
    }

    // Weather — every 10 minutes
    if (lastWeather == 0 || now - lastWeather >= 600000UL) {
        lastWeather = now;
        fetchWeather();
        drawHeader();
        drawDividers();
    }

    // Markets — every 5 minutes
    if (lastMarkets == 0 || now - lastMarkets >= 300000UL) {
        lastMarkets = now;
        fetchMarkets();
        drawAllMarkets();
        drawDividers();
    }
}
