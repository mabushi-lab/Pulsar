#pragma once
#include <Arduino.h>

// ── Foreign exchange ──────────────────────────────────────────────────────────
// Daily reference rates so a portfolio holding both EUR- and USD-quoted
// instruments totals correctly instead of refusing to add them up.
//
// Rates come from Frankfurter (ECB data, no API key, no quota). They move once
// a day, so one request a day is plenty and the table is cached in NVS —
// meaning a reboot, or a day when the API is unreachable, still converts.
const int FX_MAX      = 12;
const int FX_CODE_MAX = 6;

struct FxRate {
    char   code[FX_CODE_MAX];   // e.g. "USD"
    double perBase;             // units of `code` per 1 unit of the stored base
};

void fxBegin();                 // load the cached table from NVS
bool fxFetch();                 // one HTTPS request; true if the table updated
bool fxNeedsRefresh(uint32_t nowMs);

// Converts between any two currencies in the table. Returns false when either
// side is unknown, so callers can exclude the position instead of guessing.
bool fxConvert(double amount, const char* from, const char* to, double* out);

const char* fxBaseCode();
const char* fxDate();           // "YYYY-MM-DD" of the rates, "" if never fetched
int         fxCount();
