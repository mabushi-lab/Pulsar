#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// ── Market data ───────────────────────────────────────────────────────────────
struct MarketItem {
    const char* label;
    const char* symbol;     // Yahoo Finance symbol; exchange-traded only (no =X)
    uint32_t    accentColor;
    float       price;
    float       prevClose;  // previous close, used as the change-% reference
    float       changePct;
    bool        fetched;    // an attempt has been made (success or fail)
    bool        ok;         // we hold a displayable price — possibly a stale one
    bool        stale;      // the most recent attempt failed; price is the last good one
    const struct TradingSession* session;   // when this instrument actually trades
};

const int MARKET_COUNT = 6;
extern MarketItem markets[MARKET_COUNT];

// ── Weather state ─────────────────────────────────────────────────────────────
extern float tempC;
extern int   wxCode;
extern bool  wxFetched;

// ── Trading sessions ──────────────────────────────────────────────────────────
// An instrument's session is data, not hardcoded hours, because the 6-market grid
// and the single-instrument view do not necessarily describe the same market.
enum MarketPhase { PHASE_CLOSED, PHASE_PRE, PHASE_OPEN, PHASE_POST };

struct TradingSession {
    const char* label;      // shown in the single-instrument footer, e.g. "EURONEXT"
    bool continuous;        // false: exchange trading day;  true: near-24/5

    // Exchange day (continuous == false), minutes past local midnight
    int preStart, openStart, openEnd, postEnd;

    // Near-24/5 (continuous == true): daily break plus the weekly open and close
    int breakStart, breakEnd;
    int weekOpenWday,  weekOpenMin;
    int weekCloseWday, weekCloseMin;
};

extern const TradingSession SESSION_EQUITY;   // Xetra / Euronext Amsterdam
extern const TradingSession SESSION_METALS;   // COMEX-style, defined but unused

// Phase of `s` at an arbitrary point in the week. phaseAtMinute() is what the
// footer bar is rendered from, so the bar and the label cannot disagree.
MarketPhase phaseAtMinute(const TradingSession& s, int wday, int minute);
MarketPhase sessionPhase(const TradingSession& s, const struct tm& t);

uint32_t sessionRefreshMs(MarketPhase p);
uint32_t marketsRefreshMs();   // fastest rate any instrument's session calls for

// ── Helpers ───────────────────────────────────────────────────────────────────
const char* wmoDesc(int code);
void fmtPrice(char* buf, size_t n, float price);
void fetchWeather();

// ── Incremental market refresh ────────────────────────────────────────────────
// One HTTPS request per marketsFetchNext() call instead of six inline, so loop()
// keeps servicing buttons, the clock and the web server while a cycle runs.
void marketsStartCycle();
bool marketsCycleActive();
int  marketsFetchNext();   // index just fetched, or -1 once the cycle is finished
