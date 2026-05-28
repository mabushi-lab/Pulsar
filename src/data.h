#pragma once
#include <Arduino.h>
#include "config.h"

// ── Market data ───────────────────────────────────────────────────────────────
struct MarketItem {
    const char* label;
    const char* symbol;     // URL-encoded Yahoo Finance symbol (^ → %5E, = → %3D)
    uint32_t    accentColor;
    float       price;
    float       openPrice;
    float       changePct;
    bool        fetched;    // true after first attempt (success or fail)
    bool        ok;
};

const int MARKET_COUNT = 6;
extern MarketItem markets[MARKET_COUNT];

// ── Weather state ─────────────────────────────────────────────────────────────
extern float tempC;
extern int   wxCode;
extern bool  wxFetched;

// ── Functions ─────────────────────────────────────────────────────────────────
const char* wmoDesc(int code);
void fmtPrice(char* buf, size_t n, float price);
void fmtOpen(char* buf, size_t n, float price);
void fetchWeather();
void fetchMarkets();
