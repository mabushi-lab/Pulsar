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
    animSplash();    // typewriter title + "connecting" screen

    connectWiFi();
    setupServer();

    animRevealMain();  // staggered panel reveal instead of instant drawAll()
}

void loop() {
    checkButtons();
    server.handleClient();
    animTick();  // handles panel flash expiry at ~30 fps

    static uint32_t lastSec     = 0;
    static uint32_t lastWeather = 0;
    static uint32_t lastMarkets = 0;
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    uint32_t now    = millis();
    bool     wifiOk = (WiFi.status() == WL_CONNECTED);

    // WiFi reconnect
    if (!wifiOk) {
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
            lastRetry = now;
            connectWiFi();
            if (WiFi.status() == WL_CONNECTED) { animRevealMain(); wifiWas = true; }
            else                                  drawHeader();
        }
        return;
    }
    if (!wifiWas) { wifiWas = true; animRevealMain(); }

    // Manual market refresh via USER button or POST /refresh
    if (refreshRequested) {
        refreshRequested = false;
        lastMarkets = now;
        fetchMarkets();
        for (int i = 0; i < MARKET_COUNT; i++) triggerPanelFlash(i);
        drawAllMarkets();
        drawDividers();
    }

    // 1-second tick
    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();
        drawHeader();  // every second — drives colon blink + WiFi breathe
    }

    // Progress bar — only redraws when the minute changes
    {
        time_t     epoch = ntp.getEpochTime();
        struct tm *t     = localtime(&epoch);
        static int lastMin = -1;
        int        curMin  = t->tm_hour * 60 + t->tm_min;
        if (curMin != lastMin) { lastMin = curMin; drawProgress(); }
    }

    // Weather — every 10 minutes
    if (lastWeather == 0 || now - lastWeather >= 600000UL) {
        lastWeather = now;
        fetchWeather();
        drawHeader();
    }

    // Markets — every 5 minutes
    if (lastMarkets == 0 || now - lastMarkets >= 300000UL) {
        lastMarkets = now;
        fetchMarkets();
        for (int i = 0; i < MARKET_COUNT; i++) triggerPanelFlash(i);
        drawAllMarkets();
        drawDividers();
    }
}
