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
const int ROW_H  = 64;
const int DIV2_Y = 83;
const int ROW2_Y = 84;
const int DIV3_Y = 148;
const int PRG_Y  = 149;
const int PRG_H  = 21;

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
