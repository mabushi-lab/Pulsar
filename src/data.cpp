#include "data.h"
#include "network.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── State ─────────────────────────────────────────────────────────────────────
// All instruments are on Xetra or Euronext AMS so they share the same
// trading hours (09:00–17:30 CET) and update together.
// Yahoo Finance returns 401 for forex crosses (=X), so all symbols must
// be exchange-traded instruments.
//
//  SXR8.DE    iShares Core S&P 500 UCITS ETF      — Xetra,         EUR, TER 0.07%
//  EXW1.DE    iShares Core Euro Stoxx 50 UCITS ETF — Xetra,         EUR, TER 0.10%
//  EMIM.AS    iShares Core MSCI EM IMI UCITS ETF   — Euronext AMS,  EUR, TER 0.18%
//  VWCE.DE    Vanguard FTSE All-World UCITS ETF    — Xetra,         EUR, TER 0.22%
//  EXS1.DE    iShares Physical Gold ETC            — Xetra,         EUR, TER 0.12%
//  PHAG.AS    WisdomTree Physical Silver ETC       — Euronext AMS,  EUR, ~1 oz/share
MarketItem markets[MARKET_COUNT] = {
    { "S&P 500",   "SXR8.DE",  0x33CCFF, 0, 0, 0, false, false, false, &SESSION_EQUITY },
    { "STOXX 50",  "EXW1.DE",  0x33CCFF, 0, 0, 0, false, false, false, &SESSION_EQUITY },
    { "Emrg Mkt",  "EMIM.AS",  0x33CCFF, 0, 0, 0, false, false, false, &SESSION_EQUITY },
    { "All World", "VWCE.DE",  0x44BBFF, 0, 0, 0, false, false, false, &SESSION_EQUITY },
    { "Gold",      "EXS1.DE",  0xFFAA33, 0, 0, 0, false, false, false, &SESSION_EQUITY },
    // PHAG.AS is an ETC on Euronext Amsterdam, not spot silver: it really does
    // stop trading at 17:30 with everything else. Switching this to
    // &SESSION_METALS is only correct alongside a symbol that trades 24/5.
    { "Silver",    "PHAG.AS",  0xCCDDEE, 0, 0, 0, false, false, false, &SESSION_EQUITY },
};

float tempC    = 0.0f;
int   wxCode   = -1;
bool  wxFetched = false;

// ── Trading sessions ──────────────────────────────────────────────────────────
const TradingSession SESSION_EQUITY = {
    "EURONEXT", false,
    EQ_PRE_START, EQ_OPEN_START, EQ_OPEN_END, EQ_POST_END,
    0, 0, 0, 0, 0, 0
};

const TradingSession SESSION_METALS = {
    "COMEX", true,
    0, 0, 0, 0,
    MT_BREAK_START, MT_BREAK_END,
    MT_WEEK_OPEN_WDAY,  MT_WEEK_OPEN_MIN,
    MT_WEEK_CLOSE_WDAY, MT_WEEK_CLOSE_MIN
};

// Single source of truth for "is this market trading right now". The footer bar
// is rendered by sweeping this across the day, so the bar, the phase label and
// the polling rate are all derived from the same function.
MarketPhase phaseAtMinute(const TradingSession& s, int wday, int minute) {
    if (s.continuous) {
        if (wday == 6) return PHASE_CLOSED;                                   // Saturday
        if (wday == s.weekCloseWday && minute >= s.weekCloseMin) return PHASE_CLOSED;
        if (wday == s.weekOpenWday  && minute <  s.weekOpenMin)  return PHASE_CLOSED;
        if (minute >= s.breakStart  && minute <  s.breakEnd)     return PHASE_CLOSED;
        return PHASE_OPEN;   // continuous venues have no pre/post auction
    }
    if (wday == 0 || wday == 6) return PHASE_CLOSED;                          // weekend
    if (minute < s.preStart)   return PHASE_CLOSED;
    if (minute < s.openStart)  return PHASE_PRE;
    if (minute < s.openEnd)    return PHASE_OPEN;
    if (minute < s.postEnd)    return PHASE_POST;
    return PHASE_CLOSED;
}

MarketPhase sessionPhase(const TradingSession& s, const struct tm& t) {
    return phaseAtMinute(s, t.tm_wday, t.tm_hour * 60 + t.tm_min);
}

uint32_t sessionRefreshMs(MarketPhase p) {
    switch (p) {
        case PHASE_OPEN:            return MKT_REFRESH_OPEN_MS;
        case PHASE_PRE:
        case PHASE_POST:            return MKT_REFRESH_EDGE_MS;
        case PHASE_CLOSED: default: return MKT_REFRESH_CLOSED_MS;
    }
}

// Poll at whatever rate the most active instrument needs: if one of them trades
// around the clock, the cycle must keep up with it even when Euronext is shut.
uint32_t marketsRefreshMs() {
    struct tm t;
    if (!localNow(&t)) return MKT_REFRESH_OPEN_MS;   // pre-sync: assume the fast rate

    uint32_t fastest = MKT_REFRESH_CLOSED_MS;
    for (int i = 0; i < MARKET_COUNT; i++) {
        if (!markets[i].session) continue;
        const uint32_t ms = sessionRefreshMs(sessionPhase(*markets[i].session, t));
        if (ms < fastest) fastest = ms;
    }
    return fastest;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
const char* wmoDesc(int code) {
    if (code <  0)  return "Unknown";
    if (code == 0)  return "Clear";
    if (code <= 2)  return "Mainly Clear";
    if (code == 3)  return "Overcast";
    if (code <= 48) return "Foggy";
    if (code <= 55) return "Drizzle";
    if (code <= 65) return "Rain";
    if (code <= 75) return "Snow";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow Showers";
    if (code == 95) return "Thunderstorm";
    if (code <= 99) return "Heavy Storm";
    return "Unknown";
}

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

// ── Fetch ─────────────────────────────────────────────────────────────────────
void fetchWeather() {
    // HTTPS, like the market fetch. Over plain http:// the API answers with a
    // redirect to https, and HTTPClient does not follow redirects by default,
    // so GET() returned 301 and the header silently stayed blank.
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code",
        WEATHER_LAT, WEATHER_LON);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(6000);
    http.setConnectTimeout(6000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();
    Serial.printf("[wx]   HTTP %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getString());
        if (err) {
            Serial.printf("[wx]   json error: %s\n", err.c_str());
        } else {
            // Only commit if the fields are actually present — otherwise a
            // malformed-but-valid body would show a confident "0.0C  Clear".
            JsonVariant cur = doc["current"];
            if (cur["temperature_2m"].is<float>() && cur["weather_code"].is<int>()) {
                tempC  = cur["temperature_2m"].as<float>();
                wxCode = cur["weather_code"].as<int>();
                Serial.printf("[wx]   %.1fC  code=%d  %s\n", tempC, wxCode, wmoDesc(wxCode));
            } else {
                Serial.printf("[wx]   body missing current.temperature_2m / weather_code\n");
            }
        }
    }
    http.end();
    wxFetched = true;
}

// Yahoo Finance v8/chart — one HTTPS request per symbol.
// Called once per pass by marketsFetchNext(); a whole cycle is spread across
// MARKET_COUNT passes so nothing here ever blocks loop() for more than one request.
static void fetchOne(int i) {
    char url[128];
    snprintf(url, sizeof(url),
        "https://query2.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d",
        markets[i].symbol);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
    http.addHeader("Accept",     "application/json");

    markets[i].fetched = true;
    bool got = false;

    int httpCode = http.GET();
    Serial.printf("[fetch] %-12s  HTTP %d\n", markets[i].symbol, httpCode);
    if (httpCode == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
        filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
        filter["chart"]["result"][0]["meta"]["previousClose"]              = true;
        filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;

        JsonDocument doc;
        if (!deserializeJson(doc, *http.getStreamPtr(),
                             DeserializationOption::Filter(filter))) {
            JsonVariant meta = doc["chart"]["result"][0]["meta"];
            float price = meta["regularMarketPrice"].as<float>();
            float prev  = meta["chartPreviousClose"].as<float>();
            if (prev <= 0) prev = meta["previousClose"].as<float>();
            if (prev <= 0) prev = meta["regularMarketPreviousClose"].as<float>();
            Serial.printf("           price=%.4f  prev=%.4f\n", price, prev);
            if (price > 0) {
                markets[i].price     = price;
                markets[i].prevClose = prev;
                markets[i].changePct = (prev > 0) ? (price - prev) / prev * 100.0f : 0.0f;
                markets[i].ok        = true;
                got = true;
            }
        }
    }
    http.end();

    // A failed request keeps the last good price and only marks it stale, so one
    // 429 no longer wipes the grid to "Offline".
    markets[i].stale = !got;
    if (!got) Serial.printf("           keeping last value (stale)\n");
}

// ── Cycle driver ──────────────────────────────────────────────────────────────
static int s_cycleIdx = -1;   // -1 = idle

void marketsStartCycle() { s_cycleIdx = 0; }
bool marketsCycleActive() { return s_cycleIdx >= 0; }

int marketsFetchNext() {
    if (s_cycleIdx < 0) return -1;
    const int i = s_cycleIdx;
    fetchOne(i);
    if (++s_cycleIdx >= MARKET_COUNT) s_cycleIdx = -1;
    return i;
}
