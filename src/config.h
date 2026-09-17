#pragma once
#include <Arduino.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
const int PIN_POWER = 15;
const int BTN_BOOT  = 0;
const int BTN_USER  = 14;

// ── Layout (landscape 320×170) ────────────────────────────────────────────────
// Header (18) | div | Row1 (64) | div | Row2 (64) | div | Progress (21)
const int W      = 320;
const int H      = 170;
const int HDR_H  = 18;
const int DIV1_Y = 18;
const int ROW1_Y = 19;
const int ROW_H  = 63;   // Font4 is ~26px; 63px fits label+price+change cleanly
const int DIV2_Y = 82;
const int ROW2_Y = 83;
const int DIV3_Y = 146;
const int PRG_Y  = 147;
const int PRG_H  = 23;   // 8px bar + 1px gap + 14px Font2 label = 23px

// ── Time ──────────────────────────────────────────────────────────────────────
// POSIX TZ rule: CET in winter, CEST in summer, switching on the last Sunday
// of March and October. DST is handled by the C library, so there is no
// UTC offset to maintain by hand any more.
#define TZ_INFO      "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.cloudflare.com"

// ── Refresh intervals ─────────────────────────────────────────────────────────
// Polling adapts to the session: prices only move during continuous trading,
// so hammering Yahoo overnight and at weekends buys nothing and invites 429s.
// The footer countdown is derived from the same values, so they cannot drift.
const uint32_t MKT_REFRESH_OPEN_MS   =  15000UL;   // 15 s  — continuous trading
const uint32_t MKT_REFRESH_EDGE_MS   =  60000UL;   //  1 min — pre-market / after-hours
const uint32_t MKT_REFRESH_CLOSED_MS = 900000UL;   // 15 min — closed / weekend

// A refresh cycle fetches one symbol per pass rather than all six inline, so
// loop() keeps servicing buttons, the clock and the web server throughout.
const uint32_t MKT_SYMBOL_GAP_MS = 250UL;

const uint32_t WX_REFRESH_MS  = 600000UL;   // 10 min
const uint32_t WIFI_RETRY_MS  =  30000UL;   // 30 s between reconnect attempts

// ── Display orientation ───────────────────────────────────────────────────────
// With offset_rotation = 1 in the panel config, 0 and 2 are the two landscape
// orientations and are 180° apart; 1 and 3 are the portrait pair. Flip between
// 0 and 2 to turn the image the other way up (USB port left vs right).
// The panel is centred in the controller's 240px-wide RAM (35 + 170 + 35), so
// the column offset is symmetric and needs no adjustment when you flip.
const int DISPLAY_ROTATION = 0;

// Index of the instrument shown in the single-instrument view.
const int SILVER_MARKET_IDX = 5;

// ── Weather location ──────────────────────────────────────────────────────────
const float WEATHER_LAT = 50.8798f;  // Leuven, Belgium
const float WEATHER_LON =  4.7005f;

// ── Trading sessions ──────────────────────────────────────────────────────────
// All boundaries are wall-clock minutes in TZ_INFO, so they follow DST with it.
const int MKT_DAY_MINS = 24 * 60;          // 1440

// Equity / ETC session — Xetra and Euronext Amsterdam share these hours.
const int EQ_PRE_START  =  7 * 60;         // 07:00 pre-trading starts
const int EQ_OPEN_START =  9 * 60;         // 09:00 continuous trading
const int EQ_OPEN_END   = 17 * 60 + 30;    // 17:30 market closes
const int EQ_POST_END   = 20 * 60;         // 20:00 after-hours end (Xetra post-trading)

// Near-24/5 metals session (COMEX-style), expressed in CET/CEST:
//   opens   Sunday  23:00, closes Friday 22:00
//   daily maintenance break 22:00 -> 23:00
// Not used by the default instrument list — PHAG.AS is a Euronext-listed ETC
// and genuinely does close at 17:30. Point a MarketItem at SESSION_METALS only
// if you also switch its symbol to something that actually trades around the
// clock, or the bar will claim OPEN while the price sits frozen.
const int MT_BREAK_START = 22 * 60;        // 22:00
const int MT_BREAK_END   = 23 * 60;        // 23:00
const int MT_WEEK_OPEN_WDAY  = 0;          // Sunday
const int MT_WEEK_OPEN_MIN   = 23 * 60;    // 23:00
const int MT_WEEK_CLOSE_WDAY = 5;          // Friday
const int MT_WEEK_CLOSE_MIN  = 22 * 60;    // 22:00

// Three columns; x=107 and x=212 are 1-px vertical dividers
const int COL_X[3] = { 0, 108, 213 };
const int COL_W[3] = { 107, 104, 107 };

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
const uint32_t C_WEATHER  = 0xFFAA33;
const uint32_t C_WIFI_OK  = 0x00CC44;
const uint32_t C_WIFI_ERR = 0xFF3333;
