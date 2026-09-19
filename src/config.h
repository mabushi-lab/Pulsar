#pragma once
#include <Arduino.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
const int PIN_POWER = 15;
const int BTN_BOOT  = 0;
const int BTN_USER  = 14;

// ── Layout (landscape 320×170) ────────────────────────────────────────────────
// Header (18) | div | Row1 (63) | div | Row2 (63) | div | Footer (23)
const int W      = 320;
const int H      = 170;
const int HDR_H  = 18;
const int DIV1_Y = 18;
const int ROW1_Y = 19;
const int ROW_H  = 63;
const int DIV2_Y = 82;
const int ROW2_Y = 83;
const int DIV3_Y = 146;
const int PRG_Y  = 147;
const int PRG_H  = 23;

// Three columns; x=107 and x=212 are 1-px vertical dividers
const int COL_X[3] = { 0, 108, 213 };
const int COL_W[3] = { 107, 104, 107 };
const int GRID_SLOTS = 6;     // positions per page

// ── Display orientation ───────────────────────────────────────────────────────
// With offset_rotation = 1 in the panel config, 0 and 2 are the two landscape
// orientations and are 180° apart. Editable at runtime in Settings.
const int DISPLAY_ROTATION = 0;

// ── Time ──────────────────────────────────────────────────────────────────────
// POSIX TZ rule: DST is handled by the C library, so there is no UTC offset to
// maintain by hand. Editable at runtime in Settings.
#define TZ_INFO      "CET-1CEST,M3.5.0,M10.5.0/3"
#define MDNS_HOST    "pulsar"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.cloudflare.com"

// ── Money ─────────────────────────────────────────────────────────────────────
#define BASE_CURRENCY_DEFAULT "EUR"

// ── Starting portfolio ────────────────────────────────────────────────────────
// Seeded once, on a device with nothing stored, so a fresh flash is not an empty
// screen. All are watchlist rows (quantity 0): they are priced and displayed
// immediately, and become tracked holdings as soon as you enter a quantity and
// average cost in the web app.
//
// Xetra listings (.DE), which is where Trade Republic's German venues price
// against, plus the silver ETC on Stuttgart (.SG) where Yahoo carries it.
//   SXR8.DE  IE00B5BMR087  iShares Core S&P 500 UCITS ETF USD (Acc)
//   EUNK.DE  IE00B4K48X80  iShares Core MSCI Europe UCITS ETF EUR (Acc)
//   IS3N.DE  IE00BKM4GZ66  iShares Core MSCI EM IMI UCITS ETF USD (Acc)
//   IUSN.DE  IE00BF4RFH31  iShares MSCI World Small Cap UCITS ETF USD (Acc)
//   EUNN.DE  IE00B4L5YX21  iShares Core MSCI Japan IMI UCITS ETF USD (Acc)
//   PPFD.SG  IE00B4NCWG09  iShares Physical Silver ETC
// Fields: SYMBOL,QUANTITY,AVG COST,LABEL,TARGET%
// Targets are the intended allocation; silver has none, so it is reported
// without a drift figure rather than against an invented target of zero.
#define DEFAULT_POSITIONS \
    "SXR8.DE,0,0,S&P 500,50\n" \
    "EUNK.DE,0,0,Europe,20\n" \
    "IS3N.DE,0,0,Emrg Mkt,15\n" \
    "IUSN.DE,0,0,Small Cap,10\n" \
    "EUNN.DE,0,0,Japan,5\n" \
    "PPFD.SG,0,0,Silver\n"
// Frankfurter: ECB reference rates, no API key, no quota. Editable in Settings
// so a change of provider does not need a reflash.
#define FX_URL_DEFAULT "https://api.frankfurter.dev/v2/rates"
const uint32_t FX_REFRESH_MS = 6UL * 60UL * 60UL * 1000UL;   // 6 h; rates move daily

// How long the portfolio screen shows absolute amounts after a USER tap.
const uint32_t PORTFOLIO_REVEAL_MS = 6000UL;

// ── Refresh intervals ─────────────────────────────────────────────────────────
// Polling adapts to the session: prices only move during trading, so hammering
// the quote API overnight buys nothing and invites HTTP 429.
const uint32_t MKT_REFRESH_OPEN_MS   =  15000UL;   // 15 s
const uint32_t MKT_REFRESH_EDGE_MS   =  60000UL;   //  1 min
const uint32_t MKT_REFRESH_CLOSED_MS = 900000UL;   // 15 min

// One request per pass, so a cycle lengthens with the number of positions
// without ever lengthening a single loop() pass.
const uint32_t MKT_SYMBOL_GAP_MS = 250UL;
const uint32_t WIFI_RETRY_MS     = 30000UL;

// ── Trading sessions ──────────────────────────────────────────────────────────
// Wall-clock minutes in TZ_INFO, so they follow DST with it.
const int MKT_DAY_MINS = 24 * 60;

// European exchange day (Xetra / Euronext), in local time
const int EQ_PRE_START  =  7 * 60;
const int EQ_OPEN_START =  9 * 60;
const int EQ_OPEN_END   = 17 * 60 + 30;
const int EQ_POST_END   = 20 * 60;

// German regional exchanges (Stuttgart, Frankfurt parquet, Berlin, Munich,
// Duesseldorf, Hamburg) run far longer than Xetra — 08:00 to 22:00 — which is
// why an ETC listed there is still trading at 19:00 while Xetra is shut. Using
// Xetra's hours for them put a live position into 15-minute polling four hours
// early and labelled the footer with the wrong venue entirely.
const int DE_PRE_START  =  7 * 60 + 30;
const int DE_OPEN_START =  8 * 60;
const int DE_OPEN_END   = 22 * 60;
const int DE_POST_END   = 22 * 60 + 30;

// US cash session expressed in CET/CEST: 15:30–22:00 winter, pre from 10:00.
// Close enough for a status bar; it drifts by an hour for the weeks when US and
// EU daylight saving disagree.
const int US_PRE_START  = 10 * 60;
const int US_OPEN_START = 15 * 60 + 30;
const int US_OPEN_END   = 22 * 60;
const int US_POST_END   = 23 * 60 + 59;

// Near-24/5 metals session (COMEX-style) in CET/CEST
const int MT_BREAK_START     = 22 * 60;
const int MT_BREAK_END       = 23 * 60;
const int MT_WEEK_OPEN_WDAY  = 0;
const int MT_WEEK_OPEN_MIN   = 23 * 60;
const int MT_WEEK_CLOSE_WDAY = 5;
const int MT_WEEK_CLOSE_MIN  = 22 * 60;

// ── Night dimming ─────────────────────────────────────────────────────────────
// A 255-brightness panel on a desk is a nightlight. The schedule is wall-clock
// local time, so it follows TZ_INFO and its DST rule like everything else.
// Start and end are minutes past midnight; a window that wraps midnight (the
// normal case) is expected and handled.
const bool     NIGHT_DIM_DEFAULT   = true;
const uint8_t  NIGHT_BRIGHT_DEFAULT = 18;      // readable in the dark, not glaring
const uint16_t NIGHT_START_DEFAULT = 23 * 60;
const uint16_t NIGHT_END_DEFAULT   =  7 * 60;

// ── Loan ──────────────────────────────────────────────────────────────────────
// A staged loan drawn in equal tranches and invested. The start date has no
// sane default - everything accrues from it - so it ships empty and the loan
// view stays inert until it is set, rather than inventing a date and quietly
// reporting wrong money.
#define LOAN_START_DEFAULT   ""
#define LOAN_SYMBOLS_DEFAULT "SXR8.DE,EUNK.DE,IS3N.DE,IUSN.DE,EUNN.DE"
const float   LOAN_TRANCHE_DEFAULT  = 6000.0f;
const uint8_t LOAN_INTERVAL_DEFAULT = 6;      // months between drawdowns
const uint8_t LOAN_TRANCHES_DEFAULT = 8;      // 8 x 6k over 4 years
const float   LOAN_RATE_DEFAULT     = 1.8f;   // annual, capitalised
const int     LOAN_TRANCHE_MAX      = 40;

// ── Data sanity ───────────────────────────────────────────────────────────────
// A single-day move past this is far more likely to be a corporate action than
// a market event - a 4:1 split drops the price 75% while your quantity should
// have quadrupled, and a holdings tracker that is not told about it reports a
// catastrophic loss with total confidence. Broad index ETFs simply do not move
// 25% in a day; when the number says they did, the number is wrong.
const double SPLIT_SUSPECT_PCT = 25.0;

// Quotes more than this far apart came from different sessions, so summing them
// into one "day change" blends two different days.
const uint32_t QUOTE_SKEW_MAX_S = 36UL * 3600UL;

// ── Watchdog ──────────────────────────────────────────────────────────────────
// Long enough that no legitimate blocking call trips it: one quote is an 8 s
// connect plus an 8 s read on top of a TLS handshake. Short enough that a hung
// socket becomes a reboot rather than a device that sits there looking fine
// with a frozen clock.
const uint32_t WDT_TIMEOUT_S = 45;

// ── Colours ───────────────────────────────────────────────────────────────────
const uint32_t C_BG       = 0x06080F;
const uint32_t C_HDR      = 0x0B1420;
const uint32_t C_PANEL    = 0x080C18;
const uint32_t C_DIV      = 0x1E3A52;
const uint32_t C_PRICE    = 0xEEF2FF;
const uint32_t C_DATE     = 0x7A93AC;
const uint32_t C_LABEL    = 0x3D5A73;
const uint32_t C_UP       = 0x00CC55;
const uint32_t C_DOWN     = 0xFF3344;
const uint32_t C_MUTED    = 0x2A3F52;
const uint32_t C_ACCENT   = 0x33CCFF;
const uint32_t C_WIFI_OK  = 0x00CC44;
const uint32_t C_WIFI_ERR = 0xFF3333;
