#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "data.h"
#include "network.h"

// ── App mode ──────────────────────────────────────────────────────────────────
enum AppMode { APP_MARKET, APP_POMODORO };
static AppMode appMode = APP_MARKET;

// ── Pomodoro state ────────────────────────────────────────────────────────────
// pomState: 0=idle, 1=running, 2=paused  (matches drawPomodoro's state arg)
static int      pomState    = 0;
static bool     pomIsWork   = true;
static int      pomSecsLeft = 25 * 60;
static int      pomSessions = 0;
static uint32_t pomTick     = 0;

static int pomTotalSecs() { return pomIsWork ? 25 * 60 : 5 * 60; }

static void pomAdvanceDraw() {
    drawPomodoro(pomSecsLeft, pomTotalSecs(), pomIsWork, pomState, pomSessions);
}

static void enterPomodoro() {
    appMode     = APP_POMODORO;
    pomState    = 0;
    pomIsWork   = true;
    pomSecsLeft = 25 * 60;
    pomSessions = 0;
    pomAdvanceDraw();
}

static void exitPomodoro() {
    appMode = APP_MARKET;
    animRevealMain();
}

// ── Buttons ───────────────────────────────────────────────────────────────────
static void checkButtons() {
    static bool     prevBoot = HIGH, prevUser = HIGH;
    static uint32_t bootDown = 0,    userDown = 0;
    static uint32_t lastMs   = 0;

    if (millis() - lastMs < 50) return;
    lastMs = millis();

    bool boot = digitalRead(BTN_BOOT);
    bool user = digitalRead(BTN_USER);

    // Track press start times
    if (prevBoot == HIGH && boot == LOW) bootDown = millis();
    if (prevUser == HIGH && user == LOW) userDown = millis();

    // ── BOOT released ─────────────────────────────────────────────────────────
    if (prevBoot == LOW && boot == HIGH) {
        if (millis() - bootDown >= 600) {
            // Long press: toggle 6-market / Silver-only view
            bool sv = !displayIsSilverView();
            displaySetSilverView(sv);
            if (sv) { drawSilverOnly(); } else { drawAllMarkets(); }
            drawDividers();
        } else {
            // Short press: cycle brightness
            displayCycleBrightness();
        }
    }

    // ── USER released ─────────────────────────────────────────────────────────
    if (prevUser == LOW && user == HIGH) {
        uint32_t held = millis() - userDown;

        if (held >= 700) {
            // Long press
            if (appMode == APP_POMODORO) exitPomodoro();
            else                          refreshRequested = true;
        } else {
            // Short press
            if (appMode == APP_MARKET) {
                enterPomodoro();
            } else if (pomState == 0 || pomState == 2) {
                // Idle or paused → start / resume
                pomState = 1;
                pomTick  = millis();
                pomAdvanceDraw();
            } else {
                // Running → pause
                pomState = 2;
                pomAdvanceDraw();
            }
        }
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
    animSplash();

    connectWiFi();
    setupServer();

    animRevealMain();
}

void loop() {
    checkButtons();
    server.handleClient();

    uint32_t now = millis();

    // ── Pomodoro mode ─────────────────────────────────────────────────────────
    if (appMode == APP_POMODORO) {
        if (pomState == 1 && now - pomTick >= 1000) {
            pomTick = now;
            pomSecsLeft--;

            if (pomSecsLeft <= 0) {
                animPomAlert(pomIsWork);   // screen flash

                if (pomIsWork) {
                    pomSessions++;
                    pomIsWork   = false;
                    pomSecsLeft = 5 * 60;
                    pomState    = 1;       // auto-start break
                    pomTick     = millis();
                } else {
                    pomIsWork   = true;
                    pomSecsLeft = 25 * 60;
                    pomState    = 0;       // pause after break — user starts next session
                }
            }
            pomAdvanceDraw();
        }
        return;  // skip all market logic while timer is active
    }

    // ── Market mode ───────────────────────────────────────────────────────────
    animTick();

    static uint32_t lastSec     = 0;
    static uint32_t lastWeather = 0;
    static uint32_t lastMarkets = 0;
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    bool wifiOk = (WiFi.status() == WL_CONNECTED);

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

    // Manual market refresh
    if (refreshRequested) {
        refreshRequested = false;
        lastMarkets = now;
        fetchMarkets();
        if (displayIsSilverView()) {
            triggerPanelFlash(5);
            drawSilverOnly();
        } else {
            for (int i = 0; i < MARKET_COUNT; i++) triggerPanelFlash(i);
            drawAllMarkets();
        }
        drawDividers();
    }

    // 1-second tick: header (drives colon blink + WiFi breathe)
    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();
        drawHeader();
    }

    // Progress bar: update each minute
    {
        time_t     epoch = ntp.getEpochTime();
        struct tm *t     = localtime(&epoch);
        static int lastMin = -1;
        int curMin = t->tm_hour * 60 + t->tm_min;
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
        if (displayIsSilverView()) {
            triggerPanelFlash(5);
            drawSilverOnly();
        } else {
            for (int i = 0; i < MARKET_COUNT; i++) triggerPanelFlash(i);
            drawAllMarkets();
        }
        drawDividers();
    }
}
