#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// ── Trading sessions ──────────────────────────────────────────────────────────
// A session is data, not hardcoded hours, because positions can live on venues
// with different trading days.
enum MarketPhase { PHASE_CLOSED, PHASE_PRE, PHASE_OPEN, PHASE_POST };

struct TradingSession {
    const char* label;      // shown in the footer, e.g. "EURONEXT"
    bool continuous;        // false: exchange trading day;  true: near-24/5

    int preStart, openStart, openEnd, postEnd;          // exchange day, minutes
    int breakStart, breakEnd;                           // continuous: daily break
    int weekOpenWday,  weekOpenMin;
    int weekCloseWday, weekCloseMin;
};

// Hours are what matters; the label is what the footer says. Several European
// venues keep the same 09:00-17:30 day and differ only in name, and naming them
// correctly is the difference between a status bar you trust and one you learn
// to ignore.
extern const TradingSession SESSION_XETRA;    // 09:00-17:30, .DE
extern const TradingSession SESSION_DE_REG;   // 08:00-22:00, German regional venues
extern const TradingSession SESSION_EQUITY;   // 09:00-17:30, Euronext
extern const TradingSession SESSION_LSE;      // London, which is 09:00-17:30 in CET
extern const TradingSession SESSION_EU;       // other European venues, same day
extern const TradingSession SESSION_US;       // US cash session, in local time
extern const TradingSession SESSION_METALS;   // COMEX-style near-24/5

// Guessed from the symbol's venue suffix: ".DE"/".AS"/".PA"/".L" and friends are
// European, a bare symbol is US, "=F" is a future. Wrong guesses only affect the
// footer bar and the polling rate, never the prices.
const TradingSession* sessionForSymbol(const char* symbol);

MarketPhase phaseAtMinute(const TradingSession& s, int wday, int minute);
MarketPhase sessionPhase(const TradingSession& s, const struct tm& t);

uint32_t sessionRefreshMs(MarketPhase p);
uint32_t marketsRefreshMs();   // fastest rate any position's session calls for

// True when every held position's venue is shut right now. The day change is
// then not today's - it is the last session's, and it will not move again until
// the next open. Calling it "today" over a weekend is a claim the device cannot
// support, and it is the number people read before they read the session bar.
// False when the clock has not synced, because nothing can be asserted then.
bool marketsAllClosed();

// ── Price helpers ─────────────────────────────────────────────────────────────
void fmtPrice(char* buf, size_t n, float price);
void fmtMoney(char* buf, size_t n, double amount);   // thousands separators

// Percentages are computed from user-entered quantities, so a mistyped figure
// can produce something enormous or non-finite. Rather than letting a 300-digit
// float truncate into a display buffer, this bounds the output to something a
// 320px screen can say honestly. Never writes more than 10 bytes.
void fmtPct(char* buf, size_t n, double pct, int decimals);

// Maps a formatted number into the alphabet the large seven-segment face can
// actually draw. Font7 has exactly space, '-', '.', ':' and the digits; its own
// header says "All other characters print as a space". So a hero figure built
// with fmtMoney or fmtPct silently loses its '+', its ',' and its '%' somewhere
// between the formatter and the glass - "+1,822" arrives as "1 822" and
// "+11.1%" as "11.1", with the blanks shifting a centred string off centre.
//
// Rather than let the font edit the number, this does it deliberately: a
// thousands separator becomes a space, which is a real separator in most of
// Europe and one the font can render; a leading '+' is dropped so positives
// stay centred; anything else it cannot draw is removed. The sign that matters
// survives, because '-' is one of the glyphs it has.
void fmtSevenSeg(char* buf, size_t n, const char* src);

// Drops a trailing '%'. Drift is in percentage POINTS, not percent, so the sign
// is kept and the unit removed. fmtPct can return "--", which has no '%' to
// drop - chopping the last character blindly would leave a bare "-".
void stripPercent(char* s);

// ── Incremental refresh ───────────────────────────────────────────────────────
// One HTTPS request per call so loop() keeps servicing buttons, the clock and
// the web server while a cycle runs.
void marketsStartCycle();
bool marketsCycleActive();
int  marketsFetchNext();       // index just fetched, or -1 once finished

int      marketsCycleIndex();
int      marketsCycleLength();
uint32_t marketsCyclesCompleted();
uint32_t marketsLastCycleEndMs();
