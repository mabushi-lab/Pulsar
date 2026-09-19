#include "fx.h"
#include "config.h"
#include "settings.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

static FxRate     s_table[FX_MAX] = {};
static int        s_count         = 0;
static char       s_base[FX_CODE_MAX] = "EUR";
static char       s_date[12]      = "";
static uint32_t   s_lastFetch     = 0;
static Preferences prefs;
static const char* NVS_NS = "pulsarfx";

const char*   fxBaseCode()    { return s_base; }
const char*   fxDate()        { return s_date; }
int           fxCount()       { return s_count; }

static void upper(char* s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

// Units of `code` per 1 base unit. The base itself is 1 by definition.
static bool rateOf(const char* code, double* out) {
    char c[FX_CODE_MAX] = {};
    strncpy(c, code, FX_CODE_MAX - 1);
    upper(c);
    if (!strncmp(c, s_base, FX_CODE_MAX)) { *out = 1.0; return true; }
    for (int i = 0; i < s_count; i++) {
        if (!strncmp(c, s_table[i].code, FX_CODE_MAX) && s_table[i].perBase > 0) {
            *out = s_table[i].perBase;
            return true;
        }
    }
    return false;
}

bool fxConvert(double amount, const char* from, const char* to, double* out) {
    if (!from || !from[0] || !to || !to[0]) return false;
    // Same currency needs no table at all, so a portfolio in one currency keeps
    // working even before the first rate fetch succeeds.
    if (!strncasecmp(from, to, FX_CODE_MAX)) { *out = amount; return true; }

    double rFrom = 0, rTo = 0;
    if (!rateOf(from, &rFrom) || !rateOf(to, &rTo)) return false;
    if (rFrom <= 0) return false;
    *out = amount / rFrom * rTo;     // from -> base -> to
    return true;
}

bool fxNeedsRefresh(uint32_t nowMs) {
    if (s_lastFetch == 0) return true;
    return (nowMs - s_lastFetch) >= FX_REFRESH_MS;
}

// ── Persistence ───────────────────────────────────────────────────────────────
static void save() {
    prefs.begin(NVS_NS, false);
    prefs.putString("base", s_base);
    prefs.putString("date", s_date);
    prefs.putInt("n", s_count);
    prefs.putBytes("tbl", s_table, sizeof(FxRate) * s_count);
    prefs.end();
}

void fxBegin() {
    prefs.begin(NVS_NS, true);
    String b = prefs.getString("base", "EUR");
    String d = prefs.getString("date", "");
    s_count  = prefs.getInt("n", 0);
    if (s_count > 0 && s_count <= FX_MAX) prefs.getBytes("tbl", s_table, sizeof(FxRate) * s_count);
    else s_count = 0;
    prefs.end();

    strncpy(s_base, b.c_str(), FX_CODE_MAX - 1);
    strncpy(s_date, d.c_str(), sizeof(s_date) - 1);
    Serial.printf("[fx]    cached %d rate(s), base %s, dated %s\n",
                  s_count, s_base, s_date[0] ? s_date : "never");
}

// ── Fetch ─────────────────────────────────────────────────────────────────────
bool fxFetch() {
    char url[192];
    snprintf(url, sizeof(url), "%s?base=%s", settings.fxUrl, settings.baseCurrency);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Accept", "application/json");

    const int code = http.GET();
    Serial.printf("[fx]    HTTP %d  %s\n", code, url);
    s_lastFetch = millis();      // even a failure counts, so we do not hammer it

    if (code != HTTP_CODE_OK) { http.end(); return false; }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) { Serial.printf("[fx]    json error: %s\n", err.c_str()); return false; }

    // The provider has changed its response shape before, so accept a rates map
    // under either key and tolerate a missing base/date rather than failing.
    JsonVariant rates = doc["rates"];
    if (rates.isNull()) rates = doc["quotes"];
    if (rates.isNull()) { Serial.printf("[fx]    no rates object in response\n"); return false; }

    const char* b = doc["base"].as<const char*>();
    if (b && b[0]) { strncpy(s_base, b, FX_CODE_MAX - 1); upper(s_base); }
    else           { strncpy(s_base, settings.baseCurrency, FX_CODE_MAX - 1); upper(s_base); }

    const char* d = doc["date"].as<const char*>();
    if (d && d[0]) strncpy(s_date, d, sizeof(s_date) - 1);

    int n = 0;
    for (JsonPair kv : rates.as<JsonObject>()) {
        if (n >= FX_MAX) break;
        const double v = kv.value().as<double>();
        if (v <= 0) continue;
        memset(&s_table[n], 0, sizeof(FxRate));
        strncpy(s_table[n].code, kv.key().c_str(), FX_CODE_MAX - 1);
        upper(s_table[n].code);
        s_table[n].perBase = v;
        n++;
    }
    if (n == 0) { Serial.printf("[fx]    rates object was empty\n"); return false; }

    s_count = n;
    save();
    Serial.printf("[fx]    %d rate(s), base %s, dated %s\n", s_count, s_base, s_date);
    return true;
}
