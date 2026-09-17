#include <Arduino.h>
#include <WiFi.h>
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
            // Long press: toggle 6-market / Silver-only view.
            // Only in market mode — otherwise these draws paint market panels
            // over the Pomodoro screen, and a paused/idle timer never redraws.
            if (appMode == APP_MARKET) {
                bool sv = !displayIsSilverView();
                displaySetSilverView(sv);
                if (sv) { drawSilverOnly(); } else { drawAllMarkets(); }
                drawDividers();
            }
        } else {
            // Short press: cycle brightness (safe in either mode)
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

// ── Refresh scheduling ────────────────────────────────────────────────────────
// Both the interval and the footer countdown come from the live session phase,
// so the number on screen is always the number the scheduler is using.
static uint32_t currentRefreshMs() { return marketsRefreshMs(); }

static int refreshCountdownSecs(uint32_t now, uint32_t lastMarkets) {
    if (marketsCycleActive()) return 0;
    const uint32_t interval = currentRefreshMs();
    const uint32_t elapsed  = (lastMarkets == 0) ? interval : (now - lastMarkets);
    if (elapsed >= interval) return 0;
    return (int)((interval - elapsed) / 1000UL);
}

// ── Entry points ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(800);  // let USB CDC enumerate so early Serial output isn't lost

    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);
    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_USER, INPUT_PULLUP);

    displayInit();
    animSplash();

    wifiBegin();
    wifiWaitConnected(20000);   // the splash already says "Connecting..."
    timeBegin();
    setupServer();

    // No fetching here — loop() owns all scheduling, and its first pass starts a
    // market cycle immediately, so panels fill in one by one behind the reveal.
    animRevealMain();
}

void loop() {
    checkButtons();
    server.handleClient();

    const uint32_t now = millis();

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
    static uint32_t lastSymbol  = 0;
    // Seeded from the state setup() left behind, so the first pass does not
    // mistake "already connected" for a fresh link and replay the reveal.
    static bool     wifiWas     = wifiConnected();

    // Non-blocking: returns at once whether or not it is connected.
    wifiMaintain(now);

    const bool wifiOk = wifiConnected();
    if (wifiOk != wifiWas) {
        wifiWas = wifiOk;
        if (wifiOk) animRevealMain();
        else        drawHeader();
    }

    // Header and footer keep ticking even with no link, so the device never
    // looks frozen — only the fetches below are gated on connectivity.
    if (now - lastSec >= 1000) {
        lastSec = now;
        drawHeader();
        displaySetRefreshCountdown(refreshCountdownSecs(now, lastMarkets));
        drawProgress();
    }

    if (!wifiOk) return;

    // Manual refresh from the USER button or the web endpoint.
    if (refreshRequested && !marketsCycleActive()) {
        refreshRequested = false;
        marketsStartCycle();
    }

    // Start a market cycle when the current phase says one is due.
    if (!marketsCycleActive() &&
        (lastMarkets == 0 || now - lastMarkets >= currentRefreshMs())) {
        marketsStartCycle();
    }

    // Weather — held off while a market cycle is running so that no single
    // pass ever performs two blocking requests back to back.
    if (!marketsCycleActive() &&
        (lastWeather == 0 || now - lastWeather >= WX_REFRESH_MS)) {
        lastWeather = now;
        fetchWeather();
        drawHeader();
    }

    // Advance the cycle one symbol per pass; each panel updates as it lands.
    if (marketsCycleActive() && now - lastSymbol >= MKT_SYMBOL_GAP_MS) {
        lastSymbol = now;
        const int idx = marketsFetchNext();
        if (idx >= 0) {
            if (displayIsSilverView()) {
                if (idx == SILVER_MARKET_IDX) { triggerPanelFlash(idx); drawSilverOnly(); }
            } else {
                triggerPanelFlash(idx);
                drawMarketPanel(idx);
            }
            drawDividers();
        }
        if (!marketsCycleActive()) lastMarkets = now;   // cycle complete
    }
}
