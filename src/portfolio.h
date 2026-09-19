#pragma once
#include <Arduino.h>
#include <time.h>

// ── Positions ─────────────────────────────────────────────────────────────────
// One type for everything on screen. A position with qty == 0 is a watchlist
// entry: priced and displayed like any other, but excluded from every total.
// That keeps "things I own" and "things I watch" in one list with one editor.
//
// Trade Republic has no public API (see README), so nothing here talks to a
// broker and no credentials live on the device: you keep the position data in
// sync by hand and the device only fetches public prices.
const int POSITION_MAX = 18;
const int SYMBOL_MAX   = 16;
const int LABEL_MAX    = 12;
const int CURRENCY_MAX = 6;

struct TradingSession;   // defined in data.h

struct Position {
    char   symbol[SYMBOL_MAX];
    char   label[LABEL_MAX];     // short name for the grid; defaults to the symbol
    double qty;                  // 0 = watchlist only
    double avgCost;              // per share, in the instrument's own currency
    double target;               // target share of the portfolio, %; 0 = untracked
    char   currency[CURRENCY_MAX];

    float  price;
    float  prevClose;
    bool   fetched;              // an attempt has been made
    bool   ok;                   // a usable price is held, possibly stale
    bool   stale;                // the most recent attempt failed
    int    lastHttp;             // 0 = never attempted

    uint32_t quoteTime;          // the venue's own timestamp for this price
    const TradingSession* session;
    uint32_t accent;             // panel accent colour
};

extern Position positions[POSITION_MAX];
extern int      positionCount;

bool positionIsHeld(const Position& p);   // qty > 0
int  heldCount();

// ── Totals ────────────────────────────────────────────────────────────────────
// All figures are in the base currency from settings; positions quoted in other
// currencies are converted with ECB rates. `valid` is false until something is
// priced, so the screen can say so rather than show a confident 0.00%.
struct PortfolioTotals {
    bool   valid;
    bool   anyStale;
    bool   anyUnconverted;   // a currency with no FX rate — totals exclude it
    int    priced;
    int    unpriced;
    int    held;
    double value;
    double cost;
    double prevValue;
    double dayChangePct;
    double totalReturnPct;
    double dayChangeAbs;
    double totalReturnAbs;

    // The targeted sleeve, kept separate from the whole. An allocation of
    // 50/20/15/10/5 is defined over the funds it names; a holding with no
    // target is separate money and must not dilute it.
    double targetedValue;
    double targetSum;       // what the targets add up to, whatever that is
};

PortfolioTotals portfolioTotals();

// True only when a priced position reports a currency other than the base. An
// all-EUR portfolio therefore never fetches rates at all, so it cannot fail on
// a request it has no use for.
bool portfolioNeedsFx();

// ── Allocation ────────────────────────────────────────────────────────────────
// Current share of the portfolio against the target you set, which is the number
// that actually says whether to rebalance.
// Share of the ENTIRE portfolio, including untargeted holdings. "How much of
// everything is this."
double positionWeightPct(const Position& p, const PortfolioTotals& t);

// Share of the targeted sleeve only, and the target renormalised by the sum of
// all targets so they need not add to exactly 100. Drift is the difference.
//
// Dividing by the whole portfolio instead made every targeted position light by
// exactly the untargeted share - with physical silver at ~15%, all five funds
// read 7.5, 3.0, 2.2, 1.5 and 0.7 points short permanently, and the rebalance
// hint named the same fund forever no matter what was held.
double positionSleevePct(const Position& p, const PortfolioTotals& t);
double positionTargetPct(const Position& p, const PortfolioTotals& t);
double positionDriftPct (const Position& p, const PortfolioTotals& t);

// What it would take to bring this position back to its target, in the base
// currency. Positive means buy. This is the number a drift figure implies.
double positionRebalanceAmount(const Position& p, const PortfolioTotals& t);
bool   portfolioHasTargets();
int    worstDriftIndex(const PortfolioTotals& t);   // -1 when no targets are set

// Day change of one position, in percent; 0 when it cannot be computed.
double positionDayPct(const Position& p);

// A move too large to be a market event. Almost always a split or another
// corporate action the device has not been told about; reporting it as a loss
// would be confident and wrong.
bool positionSuspectMove(const Position& p);

// Seconds between the oldest and newest quote among priced holdings. Large
// values mean the prices being summed are not all from the same session.
uint32_t portfolioQuoteSkew();
double positionReturnPct(const Position& p);
double positionValueBase(const Position& p);   // qty × price, converted to base

// ── Persistence ───────────────────────────────────────────────────────────────
// One position per line: SYMBOL,QTY,AVGCOST[,LABEL[,TARGET%]]
// Worst case for one serialized line: symbol + label + three %.10g numbers
// (up to 18 chars each with an exponent) + separators. Sizing the editor buffer
// from this rather than a guess means a full list can never be silently
// truncated on its way into the form - which would delete positions on save.
const size_t POSITION_LINE_MAX = SYMBOL_MAX + LABEL_MAX + 3 * 18 + 8;
const size_t POSITION_TEXT_MAX = POSITION_MAX * POSITION_LINE_MAX + 1;

// True when every character is one a real ticker uses. Symbols are substituted
// straight into the quote URL, so a stray '?', '&' or space would silently
// rewrite the request's query string rather than fetch the symbol — and the
// only visible result would be a confusing HTTP error against a symbol that
// looks fine on screen. Letters, digits, '.', '-', '^' and '=' cover every
// Yahoo form: SXR8.DE, BTC-USD, ^GSPC, GC=F.
bool portfolioSymbolValid(const char* symbol);

void portfolioBegin();
bool portfolioSet(const char* text);
bool portfolioSerialize(char* buf, size_t n);   // false if it did not all fit
const char* portfolioLastError();
