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

// ── Weather location ──────────────────────────────────────────────────────────
const float WEATHER_LAT = 50.8798f;  // Leuven, Belgium
const float WEATHER_LON =  4.7005f;

// ── Market session (Euronext / Xetra, CET or CEST) ───────────────────────────
// Ensure UTC_OFFSET_SEC in secrets.h matches current offset:
//   CET  winter → 3600   CEST summer → 7200
const int MKT_PRE_START  =  7 * 60;        // 07:00 pre-trading starts
const int MKT_OPEN_START =  9 * 60;        // 09:00 continuous trading
const int MKT_OPEN_END   = 17 * 60 + 30;   // 17:30 market closes
const int MKT_POST_END   = 22 * 60;        // 22:00 after-hours end
const int MKT_DAY_MINS   = 24 * 60;        // 1440

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
