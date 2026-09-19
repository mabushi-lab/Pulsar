#pragma once
#include <Arduino.h>

// ── Runtime settings ──────────────────────────────────────────────────────────
// Everything here used to be a compile-time constant in config.h. It now lives
// in NVS and is editable from the device's own web page, so changing the
// orientation, your city or the polling rate no longer means a reflash.
// config.h still supplies the defaults.
const int TZ_MAX   = 48;
const int CCY_MAX  = 8;
const int FXURL_MAX = 96;

struct Settings {
    uint8_t  rotation;        // 0 or 2 — the two landscape orientations
    uint8_t  brightness;      // 0-255, applied at boot
    uint8_t  defaultView;     // 0 positions, 1 detail, 2 portfolio, 3 chart
    // 0 = never show amounts, 1 = reveal for a few seconds on a USER tap,
    // 2 = always on screen. A percentage hides magnitude, and magnitude is
    // usually the thing you would actually react to.
    uint8_t  amountMode;
    char     baseCurrency[8]; // everything is converted into this
    char     fxUrl[96];       // rates endpoint; a setting so a provider change
                              // does not need a reflash
    char     tz[TZ_MAX];      // POSIX TZ rule
    // Night dimming. The window is wall-clock local minutes and normally wraps
    // midnight (23:00 -> 07:00), which is why it is two numbers and not a range.
    uint8_t  nightDim;        // 0 off, 1 on
    uint8_t  nightBright;     // brightness applied inside the window
    uint16_t nightStartMin;
    uint16_t nightEndMin;

    // Loan. Borrowed money invested in a subset of the positions; the loan view
    // reports what is owed against what it bought.
    char     loanStart[12];    // "YYYY-MM-DD" of the first drawdown; empty = off
    char     loanSymbols[96];  // which positions the loan funded
    float    loanTranche;      // amount drawn each time
    float    loanRatePct;      // annual, capitalised
    uint8_t  loanIntervalM;    // months between drawdowns
    uint8_t  loanTranches;     // how many in total

    uint32_t refreshOpenMs;
    uint32_t refreshEdgeMs;
    uint32_t refreshClosedMs;
};

extern Settings settings;

void settingsBegin();          // load from NVS, falling back to config.h defaults
void settingsSave();           // persist the current struct
void settingsResetDefaults();  // back to config.h values, persisted
void settingsClamp();          // force every field into a sane range

// Applies a single "key=value" pair, clamping as needed. Returns false and
// leaves the setting untouched if the key is unknown or the value unusable.
bool settingsApply(const char* key, const char* value);

const char* settingsLastError();

// True when `minute` falls inside [startMin, endMin), with a window that wraps
// midnight handled. A zero-length window is "never", never "always" — the
// difference matters, because the degenerate case is a blank display.
bool settingsInNightWindow(int minute, int startMin, int endMin);

// Parses "HH:MM" (what <input type="time"> submits) or a plain minute count.
// Returns false rather than guessing at anything else.
bool settingsParseMinuteOfDay(const char* v, uint16_t* out);

// Boots since first flash. A reboot loop is otherwise indistinguishable from a
// device that is simply slow to fetch, which is exactly the ambiguity that made
// the unfetched-position bug hard to pin down.
uint32_t settingsBootCount();
