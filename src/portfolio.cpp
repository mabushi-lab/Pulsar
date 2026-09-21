#include "portfolio.h"
#include "data.h"
#include "fx.h"
#include "settings.h"
#include <Preferences.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

Position positions[POSITION_MAX] = {};
int      positionCount = 0;

static Preferences prefs;
// Wide enough for the longest message with the longest symbol and label
// substituted in. These messages are the whole point of rejecting a line -
// truncating one to "...cannot contain - letters, digits, . -" would leave the
// reader guessing at exactly the moment they need telling.
static char        s_lastError[192] = "";
static const char* NVS_NS  = "pulsar";
static const char* NVS_KEY = "positions";

const char* portfolioLastError() { return s_lastError; }

bool positionIsHeld(const Position& p) { return p.qty > 0; }

int heldCount() {
    int n = 0;
    for (int i = 0; i < positionCount; i++) if (positionIsHeld(positions[i])) n++;
    return n;
}

// Panel accents cycle so adjacent tiles stay distinguishable without asking the
// user to pick a colour per position.
static const uint32_t ACCENTS[] = { 0x33CCFF, 0x44BBFF, 0x66AAFF, 0xFFAA33, 0xCCDDEE, 0x66DDAA };

// ── Per-position figures ──────────────────────────────────────────────────────
double positionDayPct(const Position& p) {
    if (!p.ok || p.prevClose <= 0) return 0.0;
    return ((double)p.price - p.prevClose) / p.prevClose * 100.0;
}

double positionReturnPct(const Position& p) {
    if (!p.ok || p.avgCost <= 0) return 0.0;
    return ((double)p.price - p.avgCost) / p.avgCost * 100.0;
}

bool positionSuspectMove(const Position& p) {
    if (!p.ok || p.prevClose <= 0 || p.price <= 0) return false;
    const double d = positionDayPct(p);
    return (d > SPLIT_SUSPECT_PCT) || (d < -SPLIT_SUSPECT_PCT);
}

uint32_t portfolioQuoteSkew() {
    uint32_t lo = 0, hi = 0;
    bool any = false;
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p) || !p.ok || p.quoteTime == 0) continue;
        if (!any) { lo = hi = p.quoteTime; any = true; continue; }
        if (p.quoteTime < lo) lo = p.quoteTime;
        if (p.quoteTime > hi) hi = p.quoteTime;
    }
    return any ? (hi - lo) : 0;
}

double positionValueBase(const Position& p) {
    if (!p.ok) return 0.0;
    double v = p.qty * (double)p.price;
    double out = 0.0;
    if (fxConvert(v, p.currency, settings.baseCurrency, &out)) return out;
    return 0.0;   // caller checks anyUnconverted
}

// ── Totals ────────────────────────────────────────────────────────────────────
PortfolioTotals portfolioTotals() {
    PortfolioTotals t = {};

    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p)) continue;    // watchlist rows never affect totals
        t.held++;

        if (!p.ok || p.price <= 0) { if (p.lastHttp != 0) t.unpriced++; continue; }

        const double rawValue = p.qty * (double)p.price;
        const double rawCost  = p.qty * p.avgCost;
        const double rawPrev  = p.qty * (double)(p.prevClose > 0 ? p.prevClose : p.price);

        double value = 0, cost = 0, prev = 0;
        // Convert each leg separately so a missing rate excludes the position
        // rather than silently adding a foreign-currency number to the base.
        if (!fxConvert(rawValue, p.currency, settings.baseCurrency, &value) ||
            !fxConvert(rawCost,  p.currency, settings.baseCurrency, &cost)  ||
            !fxConvert(rawPrev,  p.currency, settings.baseCurrency, &prev)) {
            t.anyUnconverted = true;
            continue;
        }

        t.priced++;
        t.value     += value;
        t.cost      += cost;
        t.prevValue += prev;
        if (p.stale) t.anyStale = true;
    }

    // Second pass for the targeted sleeve: its denominator is itself, not the
    // whole portfolio.
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p) || p.target <= 0 || !p.ok || p.price <= 0) continue;
        double v = 0;
        if (!fxConvert(p.qty * (double)p.price, p.currency, settings.baseCurrency, &v)) continue;
        t.targetedValue += v;
        t.targetSum     += p.target;
    }

    if (t.priced == 0) return t;
    t.valid = true;

    t.dayChangeAbs   = t.value - t.prevValue;
    t.dayChangePct   = (t.prevValue > 0) ? (t.dayChangeAbs / t.prevValue) * 100.0 : 0.0;
    t.totalReturnAbs = t.value - t.cost;
    t.totalReturnPct = (t.cost > 0) ? (t.totalReturnAbs / t.cost) * 100.0 : 0.0;
    return t;
}

bool portfolioNeedsFx() {
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p) || !p.ok || !p.currency[0]) continue;
        if (strcasecmp(p.currency, settings.baseCurrency) != 0) return true;
    }
    return false;
}

// ── Allocation ────────────────────────────────────────────────────────────────
double positionWeightPct(const Position& p, const PortfolioTotals& t) {
    if (!t.valid || t.value <= 0 || !positionIsHeld(p)) return 0.0;
    const double v = positionValueBase(p);
    return (v / t.value) * 100.0;
}

double positionSleevePct(const Position& p, const PortfolioTotals& t) {
    if (!t.valid || t.targetedValue <= 0 || !positionIsHeld(p) || p.target <= 0) return 0.0;
    return (positionValueBase(p) / t.targetedValue) * 100.0;
}

// Renormalised, so a target list that adds to 95 or 105 still compares sanely
// instead of showing a constant offset on every row.
double positionTargetPct(const Position& p, const PortfolioTotals& t) {
    if (p.target <= 0 || t.targetSum <= 0) return 0.0;
    return (p.target / t.targetSum) * 100.0;
}

double positionDriftPct(const Position& p, const PortfolioTotals& t) {
    if (p.target <= 0) return 0.0;           // no target set: nothing to drift from
    return positionSleevePct(p, t) - positionTargetPct(p, t);
}

double positionRebalanceAmount(const Position& p, const PortfolioTotals& t) {
    if (p.target <= 0 || t.targetedValue <= 0) return 0.0;
    const double want = (positionTargetPct(p, t) / 100.0) * t.targetedValue;
    return want - positionValueBase(p);
}

bool portfolioHasTargets() {
    for (int i = 0; i < positionCount; i++)
        if (positionIsHeld(positions[i]) && positions[i].target > 0) return true;
    return false;
}

int worstDriftIndex(const PortfolioTotals& t) {
    int    worst = -1;
    double mag   = 0.0;
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        if (!positionIsHeld(p) || p.target <= 0 || !p.ok) continue;
        const double d = positionDriftPct(p, t);
        const double a = d < 0 ? -d : d;
        if (a > mag) { mag = a; worst = i; }
    }
    return worst;
}

// ── Parsing ───────────────────────────────────────────────────────────────────
static char* trim(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';
    return s;
}

// SYMBOL,QTY,AVGCOST[,LABEL] — blank lines and #comments ignored. Parsed into a
// scratch array so a bad line never leaves the live list half-updated.
static bool portfolioSymbolValid(const char* symbol) {
    if (!symbol || !*symbol) return false;
    for (const char* c = symbol; *c; c++) {
        const bool alnum = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                           (*c >= '0' && *c <= '9');
        if (!alnum && *c != '.' && *c != '-' && *c != '^' && *c != '=') return false;
    }
    return true;
}

// A label is free text, and it is later written verbatim into the web app's
// HTML - inside a table cell and inside a <textarea> that echoes the whole
// position list back for editing. Only '<' can open a tag (including
// "</textarea>", which would spill the rest of the page into a live editor
// field), so it is the one character actually refused here - not every
// control byte trim() leaves untouched in the middle of a string. Positions
// load through this same check at boot, so a stricter rule would risk a
// device upgrading from an older firmware failing to load an already-stored
// label it never used to reject, and doing that on an editor whose textarea
// is populated from RAM turns "one label needs fixing" into "every position
// vanished, with nothing left on screen to fix". A single, narrow, actually-
// dangerous character keeps that risk to the case that was never safe.
static bool portfolioLabelValid(const char* label) {
    for (const char* c = label; *c; c++) if (*c == '<') return false;
    return true;
}

static bool parseInto(const char* text, Position* out, int* outCount, char* err, size_t errN) {
    int count = 0, lineNo = 0;
    const char* p = text;

    while (*p) {
        char line[128];
        size_t n = 0;
        while (*p && *p != '\n' && n < sizeof(line) - 1) line[n++] = *p++;
        line[n] = '\0';
        if (*p == '\n') p++;
        lineNo++;

        char* s = trim(line);
        if (!*s || *s == '#') continue;

        if (count >= POSITION_MAX) {
            snprintf(err, errN, "too many positions (max %d)", POSITION_MAX);
            return false;
        }

        char* c1 = strchr(s, ',');
        if (!c1) { snprintf(err, errN, "line %d: expected SYMBOL,QTY,COST", lineNo); return false; }
        *c1++ = '\0';
        char* c2 = strchr(c1, ',');
        if (!c2) { snprintf(err, errN, "line %d: missing average cost", lineNo); return false; }
        *c2++ = '\0';
        char* c3 = strchr(c2, ',');      // optional label
        if (c3) *c3++ = '\0';
        char* c4 = c3 ? strchr(c3, ',') : nullptr;   // optional target %
        if (c4) *c4++ = '\0';

        char* sym  = trim(s);
        char* qtyS = trim(c1);
        char* cost = trim(c2);

        if (!*sym || strlen(sym) >= SYMBOL_MAX) {
            snprintf(err, errN, "line %d: symbol missing or longer than %d characters",
                     lineNo, SYMBOL_MAX - 1);
            return false;
        }
        if (!portfolioSymbolValid(sym)) {
            // Bounded copies, so the message cannot be cut off by a long field
            // being substituted in - the whole value of the message is that it
            // reaches its last word.
            char shown[SYMBOL_MAX];
            snprintf(shown, sizeof(shown), "%s", sym);
            snprintf(err, errN,
                     "line %d: '%s' has characters a ticker cannot contain - "
                     "letters, digits, . - ^ and = only",
                     lineNo, shown);
            return false;
        }

        // The comma is the field separator, so a decimal comma would otherwise
        // make "51,59" parse as 51 and save silently wrong.
        char* endp = nullptr;
        double qty = strtod(qtyS, &endp);
        if (endp == qtyS || *endp != '\0') {
            snprintf(err, errN, "line %d: quantity '%s' is not a number - use a dot for decimals",
                     lineNo, qtyS);
            return false;
        }
        if (qty < 0) { snprintf(err, errN, "line %d: quantity cannot be negative", lineNo); return false; }

        double avg = strtod(cost, &endp);
        if (endp == cost || *endp != '\0') {
            snprintf(err, errN, "line %d: average cost '%s' is not a number - use a dot for decimals",
                     lineNo, cost);
            return false;
        }
        if (avg < 0) { snprintf(err, errN, "line %d: average cost cannot be negative", lineNo); return false; }

        Position& q = out[count];
        memset(&q, 0, sizeof(q));
        strncpy(q.symbol, sym, SYMBOL_MAX - 1);

        // A purely numeric 4th field is a decimal comma, not a label: without
        // this, "51,59" parses as cost 51 with the label "59" and saves wrong —
        // exactly the failure the numeric checks above exist to prevent.
        char* label = c3 ? trim(c3) : nullptr;
        if (label && *label) {
            char* lend = nullptr;
            strtod(label, &lend);
            if (lend != label && *lend == '\0') {
                char shownCost[24], shownLabel[LABEL_MAX];
                snprintf(shownCost,  sizeof(shownCost),  "%s", cost);
                snprintf(shownLabel, sizeof(shownLabel), "%s", label);
                snprintf(err, errN,
                         "line %d: '%s,%s' looks like a decimal comma - use a dot, "
                         "or give the label a non-numeric name",
                         lineNo, shownCost, shownLabel);
                return false;
            }
            if (!portfolioLabelValid(label)) {
                snprintf(err, errN, "line %d: a label cannot contain '<'", lineNo);
                return false;
            }
            strncpy(q.label, label, LABEL_MAX - 1);
        } else {
            strncpy(q.label, sym, LABEL_MAX - 1);
        }

        if (c4) {
            char* tgt = trim(c4);
            if (*tgt) {
                char* tend = nullptr;
                const double tv = strtod(tgt, &tend);
                if (tend == tgt || *tend != '\0' || tv < 0 || tv > 100) {
                    snprintf(err, errN,
                             "line %d: target '%s' must be a percentage between 0 and 100",
                             lineNo, tgt);
                    return false;
                }
                q.target = tv;
            }
        }

        q.qty     = qty;
        q.avgCost = avg;
        q.session = sessionForSymbol(sym);
        q.accent  = ACCENTS[count % (int)(sizeof(ACCENTS) / sizeof(ACCENTS[0]))];
        count++;
    }

    *outCount = count;
    return true;
}

// ── Persistence ───────────────────────────────────────────────────────────────
bool portfolioSet(const char* text) {
    Position scratch[POSITION_MAX] = {};
    int      count = 0;
    s_lastError[0] = '\0';

    if (!parseInto(text, scratch, &count, s_lastError, sizeof(s_lastError))) return false;

    // Carry prices over for symbols still present, so saving an edit does not
    // blank the screen until the next refresh cycle.
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < positionCount; j++) {
            if (strncmp(scratch[i].symbol, positions[j].symbol, SYMBOL_MAX) == 0) {
                scratch[i].price     = positions[j].price;
                scratch[i].prevClose = positions[j].prevClose;
                scratch[i].fetched   = positions[j].fetched;
                scratch[i].ok        = positions[j].ok;
                scratch[i].stale     = positions[j].stale;
                scratch[i].lastHttp  = positions[j].lastHttp;
                strncpy(scratch[i].currency, positions[j].currency, CURRENCY_MAX - 1);
                break;
            }
        }
    }

    memcpy(positions, scratch, sizeof(scratch));
    positionCount = count;

    prefs.begin(NVS_NS, false);
    prefs.putString(NVS_KEY, text);
    prefs.end();
    Serial.printf("[pf]    saved %d position(s), %d held\n", count, heldCount());
    return true;
}

bool portfolioSerialize(char* buf, size_t n) {
    size_t used = 0;
    buf[0] = '\0';
    for (int i = 0; i < positionCount; i++) {
        const Position& p = positions[i];
        const bool hasLabel = strncmp(p.label, p.symbol, LABEL_MAX) != 0;
        // A target needs a label ahead of it, so an unlabelled position with a
        // target is written with its symbol as the label rather than losing it.
        const int w = (p.target > 0)
            ? snprintf(buf + used, (used < n) ? n - used : 0, "%s,%.10g,%.10g,%s,%.10g\n",
                       p.symbol, p.qty, p.avgCost, p.label, p.target)
            : hasLabel
            ? snprintf(buf + used, (used < n) ? n - used : 0, "%s,%.10g,%.10g,%s\n",
                       p.symbol, p.qty, p.avgCost, p.label)
            : snprintf(buf + used, (used < n) ? n - used : 0, "%s,%.10g,%.10g\n",
                       p.symbol, p.qty, p.avgCost);
        if (w < 0 || (size_t)w >= n - used) return false;   // ran out of room
        used += (size_t)w;
    }
    return true;
}

void portfolioBegin() {
    prefs.begin(NVS_NS, true);
    String stored  = prefs.getString(NVS_KEY, "");
    const bool seeded = prefs.getBool("seeded", false);
    prefs.end();

    // A "seeded" flag rather than an empty-string test, so a list you
    // deliberately emptied stays empty instead of the defaults coming back.
    if (stored.length() == 0) {
        if (!seeded) {
            Serial.printf("[pf]    seeding default watchlist\n");
            if (portfolioSet(DEFAULT_POSITIONS)) {
                prefs.begin(NVS_NS, false);
                prefs.putBool("seeded", true);
                prefs.end();
            }
        } else {
            Serial.printf("[pf]    no positions stored\n");
        }
        return;
    }
    Position scratch[POSITION_MAX] = {};
    int  count = 0;
    char err[96] = "";
    if (parseInto(stored.c_str(), scratch, &count, err, sizeof(err))) {
        memcpy(positions, scratch, sizeof(scratch));
        positionCount = count;
        Serial.printf("[pf]    loaded %d position(s), %d held\n", count, heldCount());
    } else {
        Serial.printf("[pf]    stored positions unreadable: %s\n", err);
    }
}
