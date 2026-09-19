#include "data.h"
#include "portfolio.h"
#include "network.h"
#include "settings.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <math.h>

// ── Trading sessions ──────────────────────────────────────────────────────────
const TradingSession SESSION_XETRA = {
    "XETRA", false,
    EQ_PRE_START, EQ_OPEN_START, EQ_OPEN_END, EQ_POST_END,
    0, 0, 0, 0, 0, 0
};

// Stuttgart and the other German regional venues trade until 22:00.
const TradingSession SESSION_DE_REG = {
    "BOERSE DE", false,
    DE_PRE_START, DE_OPEN_START, DE_OPEN_END, DE_POST_END,
    0, 0, 0, 0, 0, 0
};

const TradingSession SESSION_EQUITY = {
    "EURONEXT", false,
    EQ_PRE_START, EQ_OPEN_START, EQ_OPEN_END, EQ_POST_END,
    0, 0, 0, 0, 0, 0
};

// London runs 08:00-16:30 local, and London is one hour behind CET all year,
// so in the device's own timezone it is the same 09:00-17:30 day as Xetra.
const TradingSession SESSION_LSE = {
    "LSE", false,
    EQ_PRE_START, EQ_OPEN_START, EQ_OPEN_END, EQ_POST_END,
    0, 0, 0, 0, 0, 0
};

const TradingSession SESSION_EU = {
    "EUROPE", false,
    EQ_PRE_START, EQ_OPEN_START, EQ_OPEN_END, EQ_POST_END,
    0, 0, 0, 0, 0, 0
};

const TradingSession SESSION_US = {
    "US", false,
    US_PRE_START, US_OPEN_START, US_OPEN_END, US_POST_END,
    0, 0, 0, 0, 0, 0
};

const TradingSession SESSION_METALS = {
    "COMEX", true,
    0, 0, 0, 0,
    MT_BREAK_START, MT_BREAK_END,
    MT_WEEK_OPEN_WDAY,  MT_WEEK_OPEN_MIN,
    MT_WEEK_CLOSE_WDAY, MT_WEEK_CLOSE_MIN
};

// A wrong guess only affects the footer label and the polling rate, never a
// price, so a simple suffix test is the right amount of machinery here.
struct VenueRule { const char* suffix; const TradingSession* session; };

// Ordered as a plain table so adding a venue is a line, not a branch.
static const VenueRule VENUES[] = {
    { ".DE", &SESSION_XETRA  },                       // Xetra
    { ".SG", &SESSION_DE_REG }, { ".F",  &SESSION_DE_REG },   // Stuttgart, Frankfurt
    { ".BE", &SESSION_DE_REG }, { ".MU", &SESSION_DE_REG },   // Berlin, Munich
    { ".DU", &SESSION_DE_REG }, { ".HM", &SESSION_DE_REG },   // Duesseldorf, Hamburg
    { ".AS", &SESSION_EQUITY }, { ".PA", &SESSION_EQUITY },   // Amsterdam, Paris
    { ".BR", &SESSION_EQUITY }, { ".LS", &SESSION_EQUITY },   // Brussels, Lisbon
    { ".L",  &SESSION_LSE    },
    { ".MI", &SESSION_EU     }, { ".MC", &SESSION_EU  },      // Milan, Madrid
    { ".SW", &SESSION_EU     }, { ".VI", &SESSION_EU  },      // Zurich, Vienna
    { ".HE", &SESSION_EU     }, { ".ST", &SESSION_EU  },      // Helsinki, Stockholm
    { ".CO", &SESSION_EU     }, { ".OL", &SESSION_EU  },      // Copenhagen, Oslo
};

const TradingSession* sessionForSymbol(const char* symbol) {
    if (!symbol || !symbol[0]) return &SESSION_EU;
    if (strstr(symbol, "=F")) return &SESSION_METALS;

    const char* dot = strrchr(symbol, '.');
    if (!dot) return &SESSION_US;            // bare ticker: US listing

    for (size_t i = 0; i < sizeof(VENUES) / sizeof(VENUES[0]); i++)
        if (!strcasecmp(dot, VENUES[i].suffix)) return VENUES[i].session;

    return &SESSION_EU;                      // unknown venue: a European day, named honestly
}

MarketPhase phaseAtMinute(const TradingSession& s, int wday, int minute) {
    if (s.continuous) {
        if (wday == 6) return PHASE_CLOSED;
        if (wday == s.weekCloseWday && minute >= s.weekCloseMin) return PHASE_CLOSED;
        if (wday == s.weekOpenWday  && minute <  s.weekOpenMin)  return PHASE_CLOSED;
        if (minute >= s.breakStart  && minute <  s.breakEnd)     return PHASE_CLOSED;
        return PHASE_OPEN;
    }
    if (wday == 0 || wday == 6) return PHASE_CLOSED;
    if (minute < s.preStart)  return PHASE_CLOSED;
    if (minute < s.openStart) return PHASE_PRE;
    if (minute < s.openEnd)   return PHASE_OPEN;
    if (minute < s.postEnd)   return PHASE_POST;
    return PHASE_CLOSED;
}

MarketPhase sessionPhase(const TradingSession& s, const struct tm& t) {
    return phaseAtMinute(s, t.tm_wday, t.tm_hour * 60 + t.tm_min);
}

uint32_t sessionRefreshMs(MarketPhase p) {
    switch (p) {
        case PHASE_OPEN:            return settings.refreshOpenMs;
        case PHASE_PRE:
        case PHASE_POST:            return settings.refreshEdgeMs;
        case PHASE_CLOSED: default: return settings.refreshClosedMs;
    }
}

// Poll at whatever rate the most active position needs: if one trades while the
// others are shut, the cycle has to keep up with it.
uint32_t marketsRefreshMs() {
    struct tm t;
    if (!localNow(&t)) return settings.refreshOpenMs;
    if (positionCount == 0) return settings.refreshClosedMs;

    uint32_t fastest = settings.refreshClosedMs;
    for (int i = 0; i < positionCount; i++) {
        const TradingSession* s = positions[i].session ? positions[i].session : &SESSION_EQUITY;
        const uint32_t ms = sessionRefreshMs(sessionPhase(*s, t));
        if (ms < fastest) fastest = ms;
    }
    return fastest;
}

bool marketsAllClosed() {
    struct tm t;
    if (!localNow(&t)) return false;
    int considered = 0;
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p)) continue;      // a watchlist row moves no money
        considered++;
        const TradingSession* s = p.session ? p.session : &SESSION_EU;
        if (sessionPhase(*s, t) != PHASE_CLOSED) return false;
    }
    return considered > 0;
}

// ── Formatting ────────────────────────────────────────────────────────────────
void fmtPrice(char* buf, size_t n, float price) {
    if (price >= 1000) {
        long p = lroundf(price);
        if (p >= 1000000)
            snprintf(buf, n, "%ld,%03ld,%03ld", p / 1000000, (p / 1000) % 1000, p % 1000);
        else
            snprintf(buf, n, "%ld,%03ld", p / 1000, p % 1000);
    } else if (price >= 100) {
        snprintf(buf, n, "%.1f", price);
    } else {
        snprintf(buf, n, "%.2f", price);
    }
}

// Portfolio sums deserve separators: "12,480" reads at a glance, "12480" does not.
// Precision follows magnitude, because the same formatter prints both a total
// and a daily move: rounding a -2.34 EUR day change to "-2" throws away the part
// that tells you what actually happened, while "12,779.04" is just noise.
void fmtMoney(char* buf, size_t n, double amount) {
    const bool neg = amount < 0;
    const double mag = neg ? -amount : amount;

    if (mag < 100.0) {
        snprintf(buf, n, "%s%.2f", neg ? "-" : "", mag);
        return;
    }

    long v = lround(mag);
    char tmp[24];
    if      (v >= 1000000) snprintf(tmp, sizeof(tmp), "%ld,%03ld,%03ld", v / 1000000, (v / 1000) % 1000, v % 1000);
    else if (v >= 1000)    snprintf(tmp, sizeof(tmp), "%ld,%03ld", v / 1000, v % 1000);
    else                   snprintf(tmp, sizeof(tmp), "%ld", v);
    snprintf(buf, n, "%s%s", neg ? "-" : "", tmp);
}

// A percentage no screen should ever have to render in full. The guards are not
// theoretical: a quantity typed with one too many zeros makes prevValue tiny and
// the ratio astronomical.
void fmtPct(char* buf, size_t n, double pct, int decimals) {
    if (!(pct == pct))            { snprintf(buf, n, "--");      return; }   // NaN
    if (pct >  9999.0)            { snprintf(buf, n, ">9999%%"); return; }
    if (pct < -9999.0)            { snprintf(buf, n, "<-9999%%");return; }
    snprintf(buf, n, "%+.*f%%", decimals, pct);
}

void stripPercent(char* s) {
    if (!s || !s[0]) return;
    const size_t n = strlen(s);
    if (s[n - 1] == '%') s[n - 1] = '\0';
}

void fmtSevenSeg(char* buf, size_t n, const char* src) {
    if (!buf || n == 0) return;
    size_t w = 0;
    if (!src) { buf[0] = '\0'; return; }

    for (const char* c = src; *c && w + 1 < n; c++) {
        char out = 0;
        if ((*c >= '0' && *c <= '9') || *c == '-' || *c == '.' || *c == ':' || *c == ' ')
            out = *c;
        else if (*c == ',')                      // thousands separator it can draw
            out = ' ';
        else
            continue;                            // '+', '%', letters: it would blank them anyway
        buf[w++] = out;
    }
    buf[w] = '\0';

    // A lone "-" or an empty result says less than nothing on a 48px face.
    if (w == 0 || (w == 1 && buf[0] == '-')) snprintf(buf, n, "--");
}

// ── Quote fetch ───────────────────────────────────────────────────────────────
struct Quote {
    float price;
    float prev;
    uint32_t time;
    char  currency[8];
    bool  ok;
    int   httpCode;
};

static Quote fetchQuote(const char* symbol) {
    Quote q = {};
    char url[160];
    snprintf(url, sizeof(url),
        "https://query2.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d",
        symbol);

    WiFiClientSecure client;
    // Deliberate, not an oversight. Verifying the chain would mean pinning a
    // root CA that expires, turning a certificate rotation into a device that
    // silently stops updating. What crosses this connection is a public share
    // price and no credentials at all, so the worst a successful intercept buys
    // is a wrong number on a desk display.
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept",     "application/json");

    const int httpCode = http.GET();
    q.httpCode = httpCode;
    Serial.printf("[fetch] %-14s HTTP %d\n", symbol, httpCode);

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
        filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
        filter["chart"]["result"][0]["meta"]["previousClose"]              = true;
        filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;
        filter["chart"]["result"][0]["meta"]["currency"]                   = true;
        filter["chart"]["result"][0]["meta"]["regularMarketTime"]           = true;

        JsonDocument doc;
        if (!deserializeJson(doc, *http.getStreamPtr(),
                             DeserializationOption::Filter(filter))) {
            JsonVariant meta = doc["chart"]["result"][0]["meta"];
            const float price = meta["regularMarketPrice"].as<float>();
            float prev  = meta["chartPreviousClose"].as<float>();
            if (prev <= 0) prev = meta["previousClose"].as<float>();
            if (prev <= 0) prev = meta["regularMarketPreviousClose"].as<float>();
            const char* cur = meta["currency"].as<const char*>();
            q.time = meta["regularMarketTime"].as<uint32_t>();
            Serial.printf("          price=%.4f prev=%.4f %s\n", price, prev, cur ? cur : "?");
            if (price > 0) {
                q.price = price;
                q.prev  = prev;
                if (cur) strncpy(q.currency, cur, sizeof(q.currency) - 1);
                q.ok = true;
            }
        }
    }
    http.end();
    return q;
}

static void fetchPosition(int i) {
    Position& p = positions[i];
    const Quote q = fetchQuote(p.symbol);

    p.fetched  = true;
    p.lastHttp = q.httpCode;
    if (q.ok) {
        p.price     = q.price;
        p.prevClose = q.prev;
        p.ok        = true;
        p.quoteTime = q.time;
        if (q.currency[0]) strncpy(p.currency, q.currency, CURRENCY_MAX - 1);
    }
    // A failed request keeps the last good price and only marks it stale, so one
    // rate-limited response no longer wipes a panel of what it already knew.
    p.stale = !q.ok;
    if (!q.ok && !p.ok)
        Serial.printf("[pf]    %-14s no price (HTTP %d) - check the symbol\n", p.symbol, q.httpCode);
}

// ── Cycle driver ──────────────────────────────────────────────────────────────
static int      s_cycleIdx  = -1;
static uint32_t s_cycleDone = 0;
static uint32_t s_lastEndMs = 0;

void marketsStartCycle() { if (positionCount > 0) s_cycleIdx = 0; }
bool marketsCycleActive() { return s_cycleIdx >= 0; }

int      marketsCycleIndex()      { return s_cycleIdx; }
int      marketsCycleLength()     { return positionCount; }
uint32_t marketsCyclesCompleted() { return s_cycleDone; }
uint32_t marketsLastCycleEndMs()  { return s_lastEndMs; }

int marketsFetchNext() {
    if (s_cycleIdx < 0) return -1;
    if (s_cycleIdx >= positionCount) { s_cycleIdx = -1; return -1; }

    const int i = s_cycleIdx;
    fetchPosition(i);

    if (++s_cycleIdx >= positionCount) {
        s_cycleIdx  = -1;
        s_cycleDone++;
        s_lastEndMs = millis();
        Serial.printf("[cyc]   cycle %lu complete (%d position(s))\n",
                      (unsigned long)s_cycleDone, positionCount);
    }
    return i;
}
