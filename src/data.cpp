#include "data.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── State ─────────────────────────────────────────────────────────────────────
// All instruments are European-exchange-listed and priced in EUR.
// symbol = URL-encoded Yahoo Finance symbol  (^ → %5E, = → %3D)
//
//  SXR8.DE   iShares Core S&P 500 UCITS ETF   — Xetra,         EUR
//  ^STOXX50E Euro Stoxx 50 index               — native index,  EUR
//  EMIM.AS   iShares Core MSCI EM IMI UCITS    — Euronext AMS,  EUR
//  VWCE.DE   Vanguard FTSE All-World UCITS ETF — Xetra,         EUR
//  XAUEUR=X  Gold spot (XAU/EUR)               — Forex cross,   EUR
//  XAGEUR=X  Silver spot (XAG/EUR)             — Forex cross,   EUR
MarketItem markets[MARKET_COUNT] = {
    { "S&P 500",   "SXR8.DE",      0x33CCFF, 0, 0, 0, false, false },
    { "STOXX 50",  "%5ESTOXX50E",  0x33CCFF, 0, 0, 0, false, false },
    { "Emrg Mkt",  "EMIM.AS",      0x33CCFF, 0, 0, 0, false, false },
    { "All World", "VWCE.DE",      0x44BBFF, 0, 0, 0, false, false },
    { "Gold",      "XAUEUR%3DX",   0xFFAA33, 0, 0, 0, false, false },
    { "Silver",    "XAGEUR%3DX",   0xCCDDEE, 0, 0, 0, false, false },
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

// Shows previous-close reference price with "pc:" prefix
void fmtOpen(char* buf, size_t n, float price) {
    if (price >= 1000)
        snprintf(buf, n, "pc:%ld", lroundf(price));
    else if (price >= 100)
        snprintf(buf, n, "pc:%.1f", price);
    else
        snprintf(buf, n, "pc:%.2f", price);
}

// ── Fetch ─────────────────────────────────────────────────────────────────────
void fetchWeather() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code",
        WEATHER_LAT, WEATHER_LON);

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

// Yahoo Finance v8/chart — one HTTPS request per symbol.
// Extracts regularMarketPrice and previousClose from the meta block.
// changePct is day-over-day (price vs yesterday's close).
void fetchMarkets() {
    WiFiClientSecure client;
    client.setInsecure();

    for (int i = 0; i < MARKET_COUNT; i++) {
        char url[128];
        snprintf(url, sizeof(url),
            "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d",
            markets[i].symbol);

        HTTPClient http;
        http.setTimeout(8000);
        http.begin(client, url);
        http.addHeader("User-Agent", "Mozilla/5.0 (compatible)");
        http.addHeader("Accept",     "application/json");

        markets[i].fetched = true;
        markets[i].ok      = false;

        if (http.GET() == HTTP_CODE_OK) {
            // Filter keeps only the two meta fields we need — discards large
            // timestamp and indicator arrays before they hit the heap.
            JsonDocument filter;
            filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
            filter["chart"]["result"][0]["meta"]["previousClose"]      = true;

            JsonDocument doc;
            if (!deserializeJson(doc, http.getString(),
                                 DeserializationOption::Filter(filter))) {
                float price = doc["chart"]["result"][0]["meta"]["regularMarketPrice"].as<float>();
                float prev  = doc["chart"]["result"][0]["meta"]["previousClose"].as<float>();
                if (price > 0) {
                    markets[i].price     = price;
                    markets[i].openPrice = prev;   // stores previous close
                    markets[i].changePct = (prev > 0)
                        ? (price - prev) / prev * 100.0f : 0;
                    markets[i].ok = true;
                }
            }
        }
        http.end();
    }
}
