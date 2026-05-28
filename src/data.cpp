#include "data.h"
#include "secrets.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── State ─────────────────────────────────────────────────────────────────────
MarketItem markets[MARKET_COUNT] = {
    { "S&P 500",  "^spx",    0x33CCFF, 0, 0, 0, false, false },
    { "STOXX 50", "^sx5e",   0x33CCFF, 0, 0, 0, false, false },
    { "Emrg Mkt", "eem.us",  0x33CCFF, 0, 0, 0, false, false },
    { "Gold",     "xauusd",  0xFFAA33, 0, 0, 0, false, false },
    { "Silver",   "xagusd",  0xCCDDEE, 0, 0, 0, false, false },
    { "Bitcoin",  "btcusd",  0xF7931A, 0, 0, 0, false, false },
};

float tempC    = 0.0f;
int   wxCode   = -1;
bool  wxFetched = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
const char* wmoDesc(int code) {
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

void fmtOpen(char* buf, size_t n, float price) {
    if (price >= 1000)
        snprintf(buf, n, "o:%ld", lroundf(price));
    else if (price >= 100)
        snprintf(buf, n, "o:%.1f", price);
    else
        snprintf(buf, n, "o:%.2f", price);
}

// Extract the Nth comma-separated field from a stooq CSV line
static float csvFloat(const String& line, int field) {
    int pos = 0;
    for (int i = 0; i < field; i++) {
        pos = line.indexOf(',', pos);
        if (pos < 0) return 0;
        pos++;
    }
    int end = line.indexOf(',', pos);
    if (end < 0) end = line.length();
    return line.substring(pos, end).toFloat();
}

static String csvStr(const String& line, int field) {
    int pos = 0;
    for (int i = 0; i < field; i++) {
        pos = line.indexOf(',', pos);
        if (pos < 0) return "";
        pos++;
    }
    int end = line.indexOf(',', pos);
    if (end < 0) end = line.length();
    return line.substring(pos, end);
}

// ── Fetch ─────────────────────────────────────────────────────────────────────
void fetchWeather() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code",
        (float)LATITUDE, (float)LONGITUDE);

    HTTPClient http;
    http.setTimeout(6000);
    http.begin(url);
    if (http.GET() == HTTP_CODE_OK) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            tempC  = doc["current"]["temperature_2m"].as<float>();
            wxCode = doc["current"]["weather_code"].as<int>();
        }
    }
    http.end();
    wxFetched = true;
}

// Single HTTPS request fetches all 6 symbols from Stooq CSV API.
// CSV columns: Symbol(0), Date(1), Time(2), Open(3), High(4), Low(5), Close(6), Volume(7)
// changePct is intraday (close vs open) — the most recent trading session.
void fetchMarkets() {
    const char* url =
        "https://stooq.com/q/l/"
        "?s=%5Espx,%5Esx5e,eem.us,xauusd,xagusd,btcusd"
        "&f=sd2t2ohlcv&h&e=csv";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(client, url);
    http.addHeader("User-Agent", "Mozilla/5.0");

    bool   ok  = (http.GET() == HTTP_CODE_OK);
    String csv = ok ? http.getString() : "";
    http.end();

    for (int i = 0; i < MARKET_COUNT; i++) markets[i].fetched = true;

    if (!ok || csv.length() < 10) {
        for (int i = 0; i < MARKET_COUNT; i++) markets[i].ok = false;
        return;
    }

    // Skip the header row
    int lineStart = csv.indexOf('\n');
    if (lineStart < 0) return;
    lineStart++;

    while (lineStart < (int)csv.length()) {
        int    lineEnd = csv.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = csv.length();
        String line = csv.substring(lineStart, lineEnd);
        line.trim();

        if (line.length() > 10) {
            String sym = csvStr(line, 0);
            sym.replace("^", "");
            sym.toUpperCase();

            float openVal  = csvFloat(line, 3);
            float closeVal = csvFloat(line, 6);

            for (int i = 0; i < MARKET_COUNT; i++) {
                String mSym = String(markets[i].stooqSym);
                mSym.replace("^", "");
                mSym.toUpperCase();

                if (sym == mSym && closeVal > 0) {
                    markets[i].price     = closeVal;
                    markets[i].openPrice = openVal;
                    markets[i].changePct = (openVal > 0)
                        ? (closeVal - openVal) / openVal * 100.0f : 0;
                    markets[i].ok = true;
                    break;
                }
            }
        }
        lineStart = lineEnd + 1;
    }
}
