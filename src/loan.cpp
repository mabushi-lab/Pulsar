#include "loan.h"
#include "settings.h"
#include "portfolio.h"
#include "network.h"
#include "fx.h"
#include <string.h>
#include <strings.h>   // strcasecmp/strncasecmp live here on newlib
#include <stdlib.h>
#include <math.h>
#include <time.h>

// ── Calendar ──────────────────────────────────────────────────────────────────
// Howard Hinnant's days-from-civil. Worth using verbatim rather than counting
// leap years by hand: a drawdown schedule that slips a day every four years
// would misprice the balance in a way nobody would think to look for.
static long loanDaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

static int daysInMonth(int y, int m) {
    static const int L[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return L[m - 1];
}

// Adding six months to the 31st has to land somewhere real.
static void addMonths(int* y, int* m, int* d, int add) {
    int mm = (*m - 1) + add;
    *y += mm / 12;
    mm %= 12;
    if (mm < 0) { mm += 12; (*y)--; }
    *m = mm + 1;
    const int dim = daysInMonth(*y, *m);
    if (*d > dim) *d = dim;
}

bool loanParseDate(const char* s, int* y, int* m, int* d) {
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    const int yy = atoi(s);
    const int mm = atoi(s + 5);
    const int dd = atoi(s + 8);
    if (yy < 1970 || yy > 2200 || mm < 1 || mm > 12) return false;
    if (dd < 1 || dd > daysInMonth(yy, mm)) return false;
    *y = yy; *m = mm; *d = dd;
    return true;
}

static int loanSchedule(long today, int y, int m, int d, int intervalMonths,
                 int totalTranches, long* daysAgo, int cap, int* daysToNext) {
    if (daysToNext) *daysToNext = -1;
    if (!daysAgo || cap <= 0 || intervalMonths <= 0 || totalTranches <= 0) return 0;

    int n = 0;
    for (int i = 0; i < totalTranches; i++) {
        int yy = y, mm = m, dd = d;
        addMonths(&yy, &mm, &dd, i * intervalMonths);
        const long when = loanDaysFromCivil(yy, mm, dd);
        if (when > today) {                       // the first one not yet drawn
            if (daysToNext) *daysToNext = (int)(when - today);
            break;
        }
        if (n < cap) daysAgo[n++] = today - when;
    }
    return n;
}

// ── Compounding ───────────────────────────────────────────────────────────────
static double loanCompound(double tranche, const long* daysAgo, int n, double annualPct) {
    if (!daysAgo || n <= 0) return 0.0;
    double r = annualPct / 100.0;
    if (r < -0.999) r = -0.999;               // pow() has nothing to say below -100%
    double total = 0.0;
    for (int i = 0; i < n; i++) {
        const double years = (double)daysAgo[i] / 365.25;
        total += tranche * pow(1.0 + r, years);
    }
    return total;
}

// The rate the investments actually achieved, solved against the same drawdown
// schedule the loan is charged on. Comparing a simple total return with a
// compounding loan rate would be comparing two different things, and the
// difference flatters whichever side had its money in longest.
//
// loanCompound() rises monotonically with the rate, so bisection is exact
// enough in 80 steps and cannot diverge the way Newton can on a flat curve.
static double loanImpliedRate(double target, double tranche, const long* daysAgo, int n) {
    if (!daysAgo || n <= 0 || tranche <= 0 || !(target > 0)) return 0.0;

    // Money drawn today has earned nothing yet and implies no rate; without
    // this the curve is flat and bisection would return an arbitrary bound.
    long span = 0;
    for (int i = 0; i < n; i++) span += daysAgo[i];
    if (span <= 0) return 0.0;

    double lo = -99.0, hi = 1000.0;
    for (int it = 0; it < 80; it++) {
        const double mid = (lo + hi) / 2.0;
        if (loanCompound(tranche, daysAgo, n, mid) < target) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2.0;
}

// ── Which positions the loan bought ───────────────────────────────────────────
// Matches portfolio.cpp's own trim(): a symbol pasted into the loan-symbols
// field can carry a tab or a stray \r same as one pasted into the positions
// textarea, and the two tokenizers disagreeing on what counts as whitespace
// would fail the match silently - exactly the "typo in the loan list" failure
// this project already treats as its worst, most-repeated bug.
static bool loanIsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static bool loanFunds(const char* symbol) {
    if (!symbol || !symbol[0]) return false;
    const char* p = settings.loanSymbols;
    while (*p) {
        while (*p == ',' || loanIsSpace(*p)) p++;
        const char* start = p;
        while (*p && *p != ',') p++;
        size_t len = (size_t)(p - start);
        while (len > 0 && loanIsSpace(start[len - 1])) len--;
        if (len > 0 && strlen(symbol) == len && strncasecmp(symbol, start, len) == 0)
            return true;
    }
    return false;
}

int loanUnmatchedSymbols(char* out, size_t n) {
    if (out && n) out[0] = '\0';
    int missing = 0;
    size_t used = 0;

    const char* p = settings.loanSymbols;
    while (*p) {
        while (*p == ',' || loanIsSpace(*p)) p++;
        const char* start = p;
        while (*p && *p != ',') p++;
        size_t len = (size_t)(p - start);
        while (len > 0 && loanIsSpace(start[len - 1])) len--;
        if (len == 0) continue;

        bool found = false;
        for (int i = 0; i < positionCount && !found; i++)
            if (strlen(positions[i].symbol) == len &&
                strncasecmp(positions[i].symbol, start, len) == 0) found = true;
        if (found) continue;

        missing++;
        if (out && n && used + len + 3 < n) {
            if (used) { out[used++] = ','; out[used++] = ' '; }
            memcpy(out + used, start, len);
            used += len;
            out[used] = '\0';
        }
    }
    return missing;
}

// ── Assembly ──────────────────────────────────────────────────────────────────
LoanState loanCompute() {
    LoanState L = {};
    L.ratePct    = settings.loanRatePct;
    L.total      = settings.loanTranches;
    L.daysToNext = -1;

    int y = 0, mo = 0, d = 0;
    if (!loanParseDate(settings.loanStart, &y, &mo, &d)) return L;
    if (settings.loanTranche <= 0 || settings.loanTranches == 0) return L;

    struct tm t;
    if (!localNow(&t)) return L;      // no clock, no accrual anyone can trust
    L.configured = true;

    const long today = loanDaysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    long daysAgo[LOAN_TRANCHE_MAX];
    L.drawn = loanSchedule(today, y, mo, d, settings.loanIntervalM,
                           settings.loanTranches, daysAgo, LOAN_TRANCHE_MAX,
                           &L.daysToNext);
    if (L.drawn == 0) return L;       // configured, but the first draw is ahead

    L.principal = (double)L.drawn * (double)settings.loanTranche;
    L.owed      = loanCompound(settings.loanTranche, daysAgo, L.drawn, settings.loanRatePct);
    L.interest  = L.owed - L.principal;

    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p) || !loanFunds(p.symbol)) continue;
        if (!p.ok || p.price <= 0) { L.anyUnpriced = true; continue; }

        double v = 0, c = 0;
        if (!fxConvert(p.qty * (double)p.price, p.currency, settings.baseCurrency, &v) ||
            !fxConvert(p.qty * p.avgCost,       p.currency, settings.baseCurrency, &c)) {
            L.anyUnconverted = true;
            continue;
        }
        L.value += v;
        L.cost  += c;
    }

    L.equity    = L.value - L.owed;
    L.returnPct = loanImpliedRate(L.value, settings.loanTranche, daysAgo, L.drawn);
    L.valid     = true;
    return L;
}
