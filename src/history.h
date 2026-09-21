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
// when the closest point on file is itself more than `days` older than the
// target - a gap that wide would answer a 7-day question with a point that is
// actually weeks old. The cost basis is not for display - it is what a caller
// compares against today's to tell a market move from a deposit: a window
// return computed across a changed cost basis is a change of principal wearing
// a return's clothes, and would be wrong exactly like the "backtest of today's
// holdings" the top of this file already declined to build.
bool historyValueDaysAgo(const struct tm& today, int days, double* value, double* cost);

