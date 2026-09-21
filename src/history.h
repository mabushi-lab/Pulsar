#pragma once
#include <Arduino.h>
#include <time.h>

// ── Value history ─────────────────────────────────────────────────────────────
// One snapshot per calendar day, kept in NVS. The chart this once fed was
// replaced by Allocation and never came back, but recording never stopped,
// because a value series is the one thing on this device that cannot be
// reconstructed honestly after the fact - reconstructing it was tried, and what
// it produced was a backtest of today's holdings wearing a chart's clothes. The
// Portfolio screen's 7- and 30-day return (historyValueDaysAgo(), below) is
// what that patience paid for.
// ~4 months, not a full year: the Portfolio screen's year-to-date figure
// would want a point from January 1st still on file in December, and this
// buffer is deliberately not sized for that. The board's default partition
// table (default_16MB.csv, in the espressif32 platform package - this
// project defines no partitions.csv of its own) gives the whole device one
// 20 KB NVS partition, shared with every other namespace this firmware keeps
// - settings, the portfolio and loan text, FX rates, Wi-Fi credentials - and
// tripling this blob to cover a full year was not a trade worth making
// unverified against how much of that partition the rest of the device
// actually needs. YTD is asked for the same way 7d/30d are and simply
// declines outside roughly January-April as a result (see the caller in
// display.cpp) - correctly, per this function's own "declined rather than
// stretch a gap into an answer" rule, not as a bug.
const int HISTORY_MAX = 120;      // ~4 months; 120 × 10 bytes = 1.2 KB

struct DayPoint {
    uint16_t day;     // days since 2020-01-01, so it fits in 16 bits until 2199
    float    value;   // portfolio value in the base currency
    float    cost;    // cost basis that day
};

// NVS is flash, and flash wears out. A cycle completes every 15 s during market
// hours, and each one has a snapshot for today to record, so persisting every
// one of them would rewrite the whole blob ~2,000 times a day — enough to wear
// the partition out inside a year. The in-RAM point stays current on every
// call; only the write to flash is throttled. Losing the last few minutes of
// today's point to a reboot costs nothing, because it is recomputed from live
// prices as soon as the first cycle lands.
const uint32_t HISTORY_SAVE_MIN_MS = 600000UL;   // 10 minutes between same-day writes

void historyBegin();

// Records today's snapshot, replacing it if one already exists for today.
// Returns true when a NEW day was appended (the caller may want to redraw).
bool historyRecord(const struct tm& t, double value, double cost);

// Persists a pending same-day update immediately. Worth calling when the device
// is about to reboot on purpose, such as before an OTA update.
void historyFlush();

// Flash writes performed this boot. On a device that stores its history in
// flash, this is the number worth being able to see.
uint32_t historyWrites();

int             historyCount();
void            historyClear();

// Portfolio value and cost basis as of the closest recorded day at least
// `days` back from `today`. False before the series reaches back that far, or
// when the closest point on file is itself more than `maxGapDays` older than
// the target - a gap that wide would answer a 7-day question with a point
// that is actually weeks old. The cost basis is not for display - it is what
// a caller compares against today's to tell a market move from a deposit: a
// window return computed across a changed cost basis is a change of
// principal wearing a return's clothes, and would be wrong exactly like the
// "backtest of today's holdings" the top of this file already declined to
// build.
//
// `maxGapDays` defaults to `days` itself, which is right for a 7- or 30-day
// question - a week-old gap either way is tolerable slack for a week-long
// window. It is wrong for a long window: a year-to-date query several months
// into the year would, at the default, accept a point most of a year late and
// call it "January 1st". A caller asking a long question passes a small
// explicit `maxGapDays` instead, so the tolerance reflects the recording gap
// actually being forgiven - a device that was off for a couple of weeks -
// rather than scaling with a window length it has nothing to do with.
bool historyValueDaysAgo(const struct tm& today, int days, double* value, double* cost,
                          int maxGapDays = -1);

// The portfolio's value and cost basis as of January 1st of `today`'s year,
// from a single point captured and persisted the moment that day was actually
// recorded - not derived from the rolling buffer above, which typically
// cannot reach back that far past roughly April. False until this device has
// been running across at least one January 1st, or once a year again after
// historyClear().
bool historyYearStart(const struct tm& today, double* value, double* cost);

