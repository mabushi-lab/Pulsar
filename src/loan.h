#pragma once
#include <Arduino.h>
#include "config.h"

// ── Loan-funded investing ─────────────────────────────────────────────────────
// Part of this portfolio is bought with borrowed money, drawn in equal tranches
// at a fixed interval and charged a fixed annual rate that capitalises onto the
// balance. A portfolio screen that ignores that flatters the position by
// exactly the amount owed, and the gap grows every month.
//
// The figure that matters is NET EQUITY: value minus what it would cost to
// repay today. The second one is the spread - what the money actually returned
// against what it costs to borrow - because that is what goes negative first,
// and the loan has to be repaid whatever the market did.
struct LoanState {
    bool   configured;      // a start date is set and the schedule is usable
    bool   valid;           // ...and at least one tranche has been drawn
    int    drawn;           // tranches drawn so far
    int    total;           // tranches in the whole schedule
    int    daysToNext;      // -1 once fully drawn
    double principal;       // drawn x tranche: the cash you actually received
    double owed;            // that principal, compounded to today
    double interest;        // owed - principal
    double value;           // loan-funded positions, in the base currency
    double cost;            // what you paid for them
    double equity;          // value - owed: what is left after repaying today
    double ratePct;         // the loan's annual rate
    double returnPct;       // the rate the investments actually achieved
    // False makes returnPct a 0.0 placeholder, not a real 0% return:
    // loanImpliedRate() cannot solve a rate with nothing priced (no
    // loan-funded position currently held or priced) or with no time yet
    // elapsed (the most recent tranche was drawn today). A caller comparing
    // returnPct against the loan's own rate must check this first, or a
    // fresh drawdown reads as "the investment is already underperforming".
    bool   returnKnown;
    bool   anyUnconverted;  // a position excluded for want of an FX rate
    bool   anyUnpriced;
};

LoanState loanCompute();

// Config checks. A symbol in the loan list that matches no position is a typo
// that silently drops a holding from every loan figure while the screen still
// looks entirely healthy - the worst failure mode this device has, and the one
// it has produced three times already. Writes a comma-separated list of the
// unmatched names and returns how many there were.
int loanUnmatchedSymbols(char* out, size_t n);
bool loanParseDate(const char* s, int* y, int* m, int* d);
