#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "data.h"
#include "portfolio.h"
#include "network.h"
#include "settings.h"
#include "fx.h"
#include "history.h"

// ── Buttons ───────────────────────────────────────────────────────────────────
// BOOT  short: brightness            long: next view
// USER  short: "next" within the view  long: refresh now
static void checkButtons() {
    static bool     prevBoot = HIGH, prevUser = HIGH;
    static uint32_t bootDown = 0,    userDown = 0;
    static uint32_t lastMs   = 0;

    if (millis() - lastMs < 50) return;
    lastMs = millis();

    const bool boot = digitalRead(BTN_BOOT);
    const bool user = digitalRead(BTN_USER);

    if (prevBoot == HIGH && boot == LOW) bootDown = millis();
    if (prevUser == HIGH && user == LOW) userDown = millis();

    if (prevBoot == LOW && boot == HIGH) {
        if (millis() - bootDown >= 600) {
            displayNextView();
            displayRefreshAll();
        } else {
            displayCycleBrightness();
        }
    }

    if (prevUser == LOW && user == HIGH) {
        if (millis() - userDown >= 700) {
            refreshRequested = true;
        } else {
            displayUserAction();
            displayRefreshAll();
        }
    }

    prevBoot = boot;
    prevUser = user;
}

// ── Refresh scheduling ────────────────────────────────────────────────────────
// Both the interval and the footer countdown come from the live session phase,
// so the number on screen is the number the scheduler is using.
static uint32_t currentRefreshMs() { return marketsRefreshMs(); }

static int refreshCountdownSecs(uint32_t now, uint32_t lastMarkets) {
    if (marketsCycleActive()) return 0;
    const uint32_t interval = currentRefreshMs();
    const uint32_t elapsed  = (lastMarkets == 0) ? interval : (now - lastMarkets);
    if (elapsed >= interval) return 0;
    return (int)((interval - elapsed) / 1000UL);
}

// Records one history point per calendar day, once the portfolio is priced.
static void maybeRecordHistory() {
    struct tm t;
    if (!localNow(&t)) return;
    const PortfolioTotals tot = portfolioTotals();
    if (!tot.valid || tot.held == 0) return;
    if (historyRecord(t, tot.value, tot.cost)) {
        if (displayView() == VIEW_ALLOC) { drawAllocation(); drawDividers(); }
    }
}

// ── Entry points ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(800);  // let USB CDC enumerate so early Serial output isn't lost

    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);
    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_USER, INPUT_PULLUP);

    settingsBegin();    // before displayInit(): it owns rotation and brightness
    portfolioBegin();
    fxBegin();
    historyBegin();

    displayInit();
    animSplash();

    wifiBegin();
    wifiWaitConnected(20000);   // the splash already says "Connecting..."
    timeBegin();
    setupServer();   // starts mDNS, records the address and starts OTA if Wi-Fi
                      // made it within the wait above; loop() retries all three
                      // on the next Wi-Fi-up transition if it didn't
    watchdogBegin();   // last: everything above is allowed to take its time
    animShowAddress();
    displaySetView((DisplayView)settings.defaultView);

    // No fetching here — loop() owns all scheduling, and its first pass starts a
    // cycle immediately, so panels fill in one by one behind the reveal.
    animRevealMain();
}

void loop() {
    // First, and before anything that blocks: a firmware push has to be able to
    // interrupt whatever the device is doing, including a stuck fetch.
    watchdogFeed();
    otaHandle();
    if (otaInProgress()) return;

    checkButtons();
    server.handleClient();

    const uint32_t now = millis();
    animTick();

    static uint32_t lastSec     = 0;
    static uint32_t lastMarkets = 0;
    static uint32_t lastSymbol  = 0;
    static uint32_t lastHistory = 0;
    static bool     wifiWas     = wifiConnected();

    wifiMaintain(now);   // non-blocking: returns at once either way

    const bool wifiOk = wifiConnected();
    if (wifiOk != wifiWas) {
        wifiWas = wifiOk;
        if (wifiOk) { animRevealMain(); networkOnWifiUp(); }
        else        drawHeader();
    }

    // Header and footer keep ticking with no link, so the device never looks
    // frozen — only the fetches below are gated on connectivity.
    if (now - lastSec >= 1000) {
        lastSec = now;
        drawHeader();
        displaySetRefreshCountdown(refreshCountdownSecs(now, lastMarkets));
        drawFooter();
        displayBrightnessTick();   // no-op except at dusk and dawn
    }

    if (!wifiOk) return;

    // Manual refresh from the USER button or the web endpoint.
    if (refreshRequested && !marketsCycleActive()) {
        refreshRequested = false;
        marketsStartCycle();
    }

    // Start a cycle when the current phase says one is due.
    if (!marketsCycleActive() &&
        (lastMarkets == 0 || now - lastMarkets >= currentRefreshMs())) {
        marketsStartCycle();
    }

    // FX when something is actually quoted in another currency, or a base-
    // currency change on the settings page asked for one - and only when no
    // cycle is in flight so one pass never performs two blocking requests back
    // to back. An all-EUR portfolio never makes this request on its own.
    if (!marketsCycleActive() &&
        (fxFetchRequested || (portfolioNeedsFx() && fxNeedsRefresh(now)))) {
        fxFetchRequested = false;
        fxFetch();
        displayRefreshAll();
    }

    // Advance the cycle one symbol per pass; the screen updates as each lands.
    if (marketsCycleActive() && now - lastSymbol >= MKT_SYMBOL_GAP_MS) {
        lastSymbol = now;
        const int idx = marketsFetchNext();
        if (idx >= 0) {
            switch (displayView()) {
                case VIEW_POSITIONS: {
                    const int slot = displaySlotForPosition(idx);
                    if (slot >= 0) { triggerPanelFlash(slot); drawPositionPanel(slot); }
                    break;
                }
                case VIEW_DETAIL:
                    if (idx == displayDetailIndex()) drawDetail();
                    break;
                case VIEW_PORTFOLIO:
                    drawPortfolio();   // totals shift as each position is priced
                    break;
                case VIEW_ALLOC:
                    drawAllocation();  // weights shift as each price lands
                    break;
                case VIEW_LOAN:
                    drawLoan();        // equity moves with every price that lands
                    break;
            }
            drawHeader();              // the header carries the day change
            drawDividers();
        }
        if (!marketsCycleActive()) {
            lastMarkets = now;         // cycle complete
            maybeRecordHistory();
        }
    }

    // Belt and braces: record history even on a day with no completed cycle,
    // so a device that boots late still gets a point.
    if (now - lastHistory >= 3600000UL) {
        lastHistory = now;
        maybeRecordHistory();
    }
}
