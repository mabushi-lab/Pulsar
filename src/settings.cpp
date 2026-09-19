#include "settings.h"
#include "config.h"
#include "display.h"   // VIEW_COUNT: the view list and its bound belong together
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>
#include "loan.h"

Settings settings;

static Preferences prefs;
static char        s_err[80] = "";
static const char* NS = "pulsarcfg";

const char* settingsLastError() { return s_err; }

static uint32_t s_bootCount = 0;
uint32_t settingsBootCount() { return s_bootCount; }

// Defaults come from config.h, so the compiled-in values stay the source of
// truth for a fresh device and for "reset to defaults".
static void loadDefaults() {
    settings.rotation        = (uint8_t)DISPLAY_ROTATION;
    settings.brightness      = 255;
    settings.defaultView     = 0;
    settings.amountMode      = 2;   // amounts on screen by default
    strncpy(settings.baseCurrency, BASE_CURRENCY_DEFAULT, CCY_MAX - 1);
    settings.baseCurrency[CCY_MAX - 1] = '\0';
    strncpy(settings.fxUrl, FX_URL_DEFAULT, FXURL_MAX - 1);
    settings.fxUrl[FXURL_MAX - 1] = '\0';
    strncpy(settings.tz, TZ_INFO, TZ_MAX - 1);
    settings.tz[TZ_MAX - 1] = '\0';
    strncpy(settings.loanStart, LOAN_START_DEFAULT, sizeof(settings.loanStart) - 1);
    settings.loanStart[sizeof(settings.loanStart) - 1] = '\0';
    strncpy(settings.loanSymbols, LOAN_SYMBOLS_DEFAULT, sizeof(settings.loanSymbols) - 1);
    settings.loanSymbols[sizeof(settings.loanSymbols) - 1] = '\0';
    settings.loanTranche     = LOAN_TRANCHE_DEFAULT;
    settings.loanRatePct     = LOAN_RATE_DEFAULT;
    settings.loanIntervalM   = LOAN_INTERVAL_DEFAULT;
    settings.loanTranches    = LOAN_TRANCHES_DEFAULT;
    settings.nightDim        = NIGHT_DIM_DEFAULT ? 1 : 0;
    settings.nightBright     = NIGHT_BRIGHT_DEFAULT;
    settings.nightStartMin   = NIGHT_START_DEFAULT;
    settings.nightEndMin     = NIGHT_END_DEFAULT;
    settings.refreshOpenMs   = MKT_REFRESH_OPEN_MS;
    settings.refreshEdgeMs   = MKT_REFRESH_EDGE_MS;
    settings.refreshClosedMs = MKT_REFRESH_CLOSED_MS;
}

// Nothing downstream should have to defend itself against a bad stored value.
void settingsClamp() {
    if (settings.rotation != 0 && settings.rotation != 2) settings.rotation = 0;
    if (settings.defaultView >= VIEW_COUNT) settings.defaultView = 0;
    if (settings.amountMode  > 2) settings.amountMode  = 2;
    if (settings.nightDim    > 1) settings.nightDim    = 1;
    if (settings.loanIntervalM == 0 || settings.loanIntervalM > 24) settings.loanIntervalM = LOAN_INTERVAL_DEFAULT;
    if (settings.loanTranches > LOAN_TRANCHE_MAX) settings.loanTranches = LOAN_TRANCHE_MAX;
    if (!(settings.loanTranche > 0))   settings.loanTranche = 0;      // 0 disables
    if (!(settings.loanRatePct >= 0) || settings.loanRatePct > 100) settings.loanRatePct = LOAN_RATE_DEFAULT;
    // A stored value past the end of the day would make the window untestable
    // rather than merely wrong, so it is folded back instead of trusted.
    if (settings.nightStartMin >= MKT_DAY_MINS) settings.nightStartMin = NIGHT_START_DEFAULT;
    if (settings.nightEndMin   >= MKT_DAY_MINS) settings.nightEndMin   = NIGHT_END_DEFAULT;
    if (settings.tz[0] == '\0') strncpy(settings.tz, TZ_INFO, TZ_MAX - 1);
    if (settings.baseCurrency[0] == '\0') strncpy(settings.baseCurrency, BASE_CURRENCY_DEFAULT, CCY_MAX - 1);
    if (settings.fxUrl[0] == '\0') strncpy(settings.fxUrl, FX_URL_DEFAULT, FXURL_MAX - 1);

    // Floors exist because a cycle is one request per position: polling faster
    // than a cycle can finish would only queue requests behind themselves.
    const uint32_t LO = 5000UL, HI = 3600000UL;
    if (settings.refreshOpenMs   < LO) settings.refreshOpenMs   = LO;
    if (settings.refreshOpenMs   > HI) settings.refreshOpenMs   = HI;
    if (settings.refreshEdgeMs   < LO) settings.refreshEdgeMs   = LO;
    if (settings.refreshEdgeMs   > HI) settings.refreshEdgeMs   = HI;
    if (settings.refreshClosedMs < LO) settings.refreshClosedMs = LO;
    if (settings.refreshClosedMs > HI) settings.refreshClosedMs = HI;
}

void settingsSave() {
    settingsClamp();
    prefs.begin(NS, false);
    prefs.putUChar ("rot",   settings.rotation);
    prefs.putUChar ("bri",   settings.brightness);
    prefs.putUChar ("view",  settings.defaultView);
    prefs.putUChar ("amt",   settings.amountMode);
    prefs.putString("ccy",   settings.baseCurrency);
    prefs.putString("fxurl", settings.fxUrl);
    prefs.putString("tz",    settings.tz);
    prefs.putString("lStart", settings.loanStart);
    prefs.putString("lSyms",  settings.loanSymbols);
    prefs.putFloat ("lAmt",   settings.loanTranche);
    prefs.putFloat ("lRate",  settings.loanRatePct);
    prefs.putUChar ("lIvl",   settings.loanIntervalM);
    prefs.putUChar ("lN",     settings.loanTranches);
    prefs.putUChar ("nDim",  settings.nightDim);
    prefs.putUChar ("nBri",  settings.nightBright);
    prefs.putUShort("nStart",settings.nightStartMin);
    prefs.putUShort("nEnd",  settings.nightEndMin);
    prefs.putULong ("rOpen", settings.refreshOpenMs);
    prefs.putULong ("rEdge", settings.refreshEdgeMs);
    prefs.putULong ("rShut", settings.refreshClosedMs);
    prefs.end();
    Serial.printf("[cfg]   saved\n");
}

static void copyString(char* dst, size_t n, const String& v, const char* fallback) {
    if (v.length() > 0 && v.length() < n) { strncpy(dst, v.c_str(), n - 1); dst[n - 1] = '\0'; }
    else { strncpy(dst, fallback, n - 1); dst[n - 1] = '\0'; }
}

void settingsBegin() {
    loadDefaults();
    prefs.begin(NS, true);
    settings.rotation        = prefs.getUChar ("rot",   settings.rotation);
    settings.brightness      = prefs.getUChar ("bri",   settings.brightness);
    settings.defaultView     = prefs.getUChar ("view",  settings.defaultView);
    settings.amountMode      = prefs.getUChar ("amt",   settings.amountMode);
    String ccy               = prefs.getString("ccy",   settings.baseCurrency);
    String fxu               = prefs.getString("fxurl", settings.fxUrl);
    String tz                = prefs.getString("tz",    settings.tz);
    String lst               = prefs.getString("lStart", settings.loanStart);
    String lsy               = prefs.getString("lSyms",  settings.loanSymbols);
    settings.loanTranche     = prefs.getFloat ("lAmt",   settings.loanTranche);
    settings.loanRatePct     = prefs.getFloat ("lRate",  settings.loanRatePct);
    settings.loanIntervalM   = prefs.getUChar ("lIvl",   settings.loanIntervalM);
    settings.loanTranches    = prefs.getUChar ("lN",     settings.loanTranches);
    settings.nightDim        = prefs.getUChar ("nDim",  settings.nightDim);
    settings.nightBright     = prefs.getUChar ("nBri",  settings.nightBright);
    settings.nightStartMin   = prefs.getUShort("nStart",settings.nightStartMin);
    settings.nightEndMin     = prefs.getUShort("nEnd",  settings.nightEndMin);
    settings.refreshOpenMs   = prefs.getULong ("rOpen", settings.refreshOpenMs);
    settings.refreshEdgeMs   = prefs.getULong ("rEdge", settings.refreshEdgeMs);
    settings.refreshClosedMs = prefs.getULong ("rShut", settings.refreshClosedMs);
    prefs.end();

    // Counted here because settingsBegin() is the first thing setup() calls.
    prefs.begin(NS, false);
    s_bootCount = prefs.getULong("boots", 0) + 1;
    prefs.putULong("boots", s_bootCount);
    prefs.end();

    // An empty start date is meaningful - it is what keeps the loan view inert -
    // so it is copied verbatim rather than through copyString, which would
    // substitute the fallback and switch the feature on with a wrong date.
    strncpy(settings.loanStart, lst.c_str(), sizeof(settings.loanStart) - 1);
    settings.loanStart[sizeof(settings.loanStart) - 1] = '\0';
    copyString(settings.loanSymbols, sizeof(settings.loanSymbols), lsy, LOAN_SYMBOLS_DEFAULT);
    copyString(settings.baseCurrency, CCY_MAX,   ccy, BASE_CURRENCY_DEFAULT);
    copyString(settings.fxUrl,        FXURL_MAX, fxu, FX_URL_DEFAULT);
    copyString(settings.tz,           TZ_MAX,    tz,  TZ_INFO);
    settingsClamp();
    Serial.printf("[cfg]   boot #%lu  rot=%u bri=%u view=%u base=%s tz=%s\n",
                  (unsigned long)s_bootCount,
                  settings.rotation, settings.brightness, settings.defaultView,
                  settings.baseCurrency, settings.tz);
}

void settingsResetDefaults() { loadDefaults(); settingsSave(); }

// ── Night window ──────────────────────────────────────────────────────────────
// Written as a free function taking its bounds as arguments so the wrap-around
// case can be tested directly; the schedule that decides whether the screen is
// lit is not something to verify by staying up until 23:00.
bool settingsInNightWindow(int minute, int startMin, int endMin) {
    if (startMin == endMin) return false;                 // zero-length: never
    if (startMin <  endMin) return minute >= startMin && minute < endMin;
    return minute >= startMin || minute < endMin;         // wraps midnight
}

bool settingsParseMinuteOfDay(const char* v, uint16_t* out) {
    if (!v || !v[0]) return false;
    const char* colon = strchr(v, ':');
    char* end = nullptr;
    const unsigned long a = strtoul(v, &end, 10);
    if (end == v) return false;

    if (colon) {
        if (end != colon) return false;
        const char* mp = colon + 1;
        const unsigned long b = strtoul(mp, &end, 10);
        if (end == mp || *end != '\0') return false;
        if (a > 23 || b > 59) return false;
        *out = (uint16_t)(a * 60 + b);
        return true;
    }
    if (*end != '\0' || a >= (unsigned long)MKT_DAY_MINS) return false;
    *out = (uint16_t)a;
    return true;
}

// ── Form application ──────────────────────────────────────────────────────────
static bool toULong(const char* v, unsigned long* out) {
    char* end = nullptr;
    const unsigned long n = strtoul(v, &end, 10);
    if (end == v || *end != '\0') return false;
    *out = n;
    return true;
}

bool settingsApply(const char* key, const char* value) {
    s_err[0] = '\0';
    unsigned long n = 0;

    if (!strcmp(key, "rot")) {
        if (!toULong(value, &n) || (n != 0 && n != 2)) { snprintf(s_err, sizeof(s_err), "rotation must be 0 or 2"); return false; }
        settings.rotation = (uint8_t)n; return true;
    }
    if (!strcmp(key, "bri")) {
        if (!toULong(value, &n) || n > 255) { snprintf(s_err, sizeof(s_err), "brightness must be 0-255"); return false; }
        settings.brightness = (uint8_t)n; return true;
    }
    if (!strcmp(key, "view")) {
        if (!toULong(value, &n) || n >= (unsigned long)VIEW_COUNT) {
            snprintf(s_err, sizeof(s_err), "view must be 0-%d", VIEW_COUNT - 1); return false;
        }
        settings.defaultView = (uint8_t)n; return true;
    }
    if (!strcmp(key, "amt")) {
        if (!toULong(value, &n) || n > 2) { snprintf(s_err, sizeof(s_err), "amount mode must be 0-2"); return false; }
        settings.amountMode = (uint8_t)n; return true;
    }
    if (!strcmp(key, "nDim")) {
        if (!toULong(value, &n) || n > 1) { snprintf(s_err, sizeof(s_err), "night dimming must be 0 or 1"); return false; }
        settings.nightDim = (uint8_t)n; return true;
    }
    if (!strcmp(key, "nBri")) {
        if (!toULong(value, &n) || n > 255) { snprintf(s_err, sizeof(s_err), "night brightness must be 0-255"); return false; }
        settings.nightBright = (uint8_t)n; return true;
    }
    if (!strcmp(key, "nStart") || !strcmp(key, "nEnd")) {
        uint16_t m = 0;
        if (!settingsParseMinuteOfDay(value, &m)) {
            snprintf(s_err, sizeof(s_err), "night times must be HH:MM"); return false;
        }
        if (!strcmp(key, "nStart")) settings.nightStartMin = m;
        else                        settings.nightEndMin   = m;
        return true;
    }
    if (!strcmp(key, "lStart")) {
        // Empty is legitimate and meaningful: it switches the loan view off.
        if (!value[0]) { settings.loanStart[0] = '\0'; return true; }
        int y = 0, m = 0, d = 0;
        if (!loanParseDate(value, &y, &m, &d)) {
            snprintf(s_err, sizeof(s_err), "drawdown date must be YYYY-MM-DD"); return false;
        }
        strncpy(settings.loanStart, value, sizeof(settings.loanStart) - 1);
        settings.loanStart[sizeof(settings.loanStart) - 1] = '\0';
        return true;
    }
    if (!strcmp(key, "lSyms")) {
        if (strlen(value) >= sizeof(settings.loanSymbols)) {
            snprintf(s_err, sizeof(s_err), "loan symbol list too long"); return false;
        }
        strncpy(settings.loanSymbols, value, sizeof(settings.loanSymbols) - 1);
        settings.loanSymbols[sizeof(settings.loanSymbols) - 1] = '\0';
        return true;
    }
    if (!strcmp(key, "lAmt") || !strcmp(key, "lRate")) {
        char* end = nullptr;
        const double v = strtod(value, &end);
        if (end == value || *end != '\0' || v < 0) {
            snprintf(s_err, sizeof(s_err), "%s must be a number - use a dot for decimals", key);
            return false;
        }
        if (!strcmp(key, "lAmt"))  { settings.loanTranche = (float)v; return true; }
        if (v > 100) { snprintf(s_err, sizeof(s_err), "interest rate must be 0-100"); return false; }
        settings.loanRatePct = (float)v;
        return true;
    }
    if (!strcmp(key, "lIvl")) {
        if (!toULong(value, &n) || n < 1 || n > 24) { snprintf(s_err, sizeof(s_err), "interval must be 1-24 months"); return false; }
        settings.loanIntervalM = (uint8_t)n; return true;
    }
    if (!strcmp(key, "lN")) {
        if (!toULong(value, &n) || n > (unsigned long)LOAN_TRANCHE_MAX) {
            snprintf(s_err, sizeof(s_err), "tranche count must be 0-%d", LOAN_TRANCHE_MAX); return false;
        }
        settings.loanTranches = (uint8_t)n; return true;
    }
    if (!strcmp(key, "ccy")) {
        const size_t len = strlen(value);
        if (len < 3 || len >= CCY_MAX) { snprintf(s_err, sizeof(s_err), "currency must be a 3-letter code"); return false; }
        for (size_t i = 0; i < len; i++) if (!isalpha((unsigned char)value[i])) {
            snprintf(s_err, sizeof(s_err), "currency must be letters only"); return false;
        }
        strncpy(settings.baseCurrency, value, CCY_MAX - 1);
        settings.baseCurrency[CCY_MAX - 1] = '\0';
        for (char* c = settings.baseCurrency; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        return true;
    }
    if (!strcmp(key, "fxurl")) {
        if (strncmp(value, "https://", 8) != 0 || strlen(value) >= FXURL_MAX) {
            snprintf(s_err, sizeof(s_err), "rates URL must be https and under %d chars", FXURL_MAX);
            return false;
        }
        strncpy(settings.fxUrl, value, FXURL_MAX - 1); settings.fxUrl[FXURL_MAX - 1] = '\0'; return true;
    }
    if (!strcmp(key, "tz")) {
        if (!value[0] || strlen(value) >= TZ_MAX) { snprintf(s_err, sizeof(s_err), "timezone empty or too long"); return false; }
        strncpy(settings.tz, value, TZ_MAX - 1); settings.tz[TZ_MAX - 1] = '\0'; return true;
    }
    if (!strcmp(key, "rOpen") || !strcmp(key, "rEdge") || !strcmp(key, "rShut")) {
        if (!toULong(value, &n) || n < 5 || n > 3600) { snprintf(s_err, sizeof(s_err), "interval must be 5-3600 seconds"); return false; }
        const uint32_t ms = (uint32_t)n * 1000UL;
        if      (!strcmp(key, "rOpen")) settings.refreshOpenMs   = ms;
        else if (!strcmp(key, "rEdge")) settings.refreshEdgeMs   = ms;
        else                            settings.refreshClosedMs = ms;
        return true;
    }

    snprintf(s_err, sizeof(s_err), "unknown setting '%s'", key);
    return false;
}
