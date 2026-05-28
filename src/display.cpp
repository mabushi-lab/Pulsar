#include "display.h"
#include "config.h"
#include "data.h"
#include "network.h"
#include <LovyanGFX.hpp>
#include <WiFi.h>

// ── LGFX (T-Display S3 canonical 8-bit parallel config) ──────────────────────
class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_Parallel8 _bus;
    lgfx::Panel_ST7789  _panel;
    lgfx::Light_PWM     _bl;
public:
    LGFX() {
        {
            auto cfg    = _bus.config();
            cfg.pin_wr  = 8;  cfg.pin_rd = 9;  cfg.pin_rs = 7;
            cfg.pin_d0  = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
            cfg.pin_d4  = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = 6;
            cfg.pin_rst         = 5;
            cfg.panel_width     = 170;
            cfg.panel_height    = 320;
            cfg.offset_x        = 35;
            cfg.offset_rotation = 1;
            cfg.invert          = true;
            cfg.readable        = false;
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            _panel.config(cfg);
        }
        {
            auto cfg        = _bl.config();
            cfg.pin_bl      = 38;
            cfg.invert      = false;
            cfg.freq        = 22000;
            cfg.pwm_channel = 7;
            _bl.config(cfg);
            _panel.setLight(&_bl);
        }
        setPanel(&_panel);
    }
};

static LGFX lcd;

// ── Public display control ────────────────────────────────────────────────────
void displayInit() {
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);
}

void displaySetBrightness(bool full) {
    lcd.setBrightness(full ? 255 : 60);
}

void drawSplash(const char* msg) {
    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString(msg, W / 2, H / 2);
}

// ── Structural elements ───────────────────────────────────────────────────────
void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    lcd.drawFastVLine(107, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(107, ROW2_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW2_Y, ROW_H, C_DIV);
}

// ── Section draws ─────────────────────────────────────────────────────────────
void drawHeader() {
    lcd.fillRect(0, 0, W, HDR_H, C_HDR);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);
    char       tdBuf[24];
    strftime(tdBuf, sizeof(tdBuf), "%H:%M  %a %b %d", t);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.setTextDatum(lgfx::middle_left);
    lcd.drawString(tdBuf, 5, HDR_H / 2);

    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    lcd.fillCircle(W - 6, HDR_H / 2, 3, wifiOk ? C_WIFI_OK : C_WIFI_ERR);

    if (wxCode >= 0) {
        char wx[22];
        snprintf(wx, sizeof(wx), "%.1f%cC  %s", tempC, '\xB0', wmoDesc(wxCode));
        lcd.setTextColor(C_WEATHER, C_HDR);
        lcd.setTextDatum(lgfx::middle_right);
        lcd.drawString(wx, W - 14, HDR_H / 2);
    }
}

void drawMarketPanel(int idx) {
    const MarketItem& m   = markets[idx];
    int               col = idx % 3;
    int               x   = COL_X[col];
    int               w   = COL_W[col];
    int               y   = (idx < 3) ? ROW1_Y : ROW2_Y;
    int               cx  = x + w / 2;

    lcd.fillRect(x, y, w, ROW_H, C_PANEL);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(m.accentColor, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(m.label, cx, y + 3);

    if (!m.fetched) {
        lcd.setTextColor(C_MUTED, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Loading...", cx, y + ROW_H / 2);
        return;
    }
    if (!m.ok) {
        lcd.setTextColor(C_DOWN, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Offline", cx, y + ROW_H / 2);
        return;
    }

    char priceBuf[12];
    fmtPrice(priceBuf, sizeof(priceBuf), m.price);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_PRICE, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, y + 14);

    char changeBuf[10];
    snprintf(changeBuf, sizeof(changeBuf), "%+.2f%%", m.changePct);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(m.changePct >= 0 ? C_UP : C_DOWN, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(changeBuf, cx, y + 33);

    char openBuf[12];
    fmtOpen(openBuf, sizeof(openBuf), m.openPrice);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.drawString(openBuf, cx, y + 47);
}

void drawAllMarkets() {
    for (int i = 0; i < MARKET_COUNT; i++) drawMarketPanel(i);
}

// 4-phase market session bar (Euronext / Xetra, CET/CEST).
// Top half: full-width segmented bar with a cursor at the current minute.
// Bottom half: phase label + countdown to the next transition.
void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    time_t     epoch   = ntp.getEpochTime();
    struct tm *t       = localtime(&epoch);
    int        curMin  = t->tm_hour * 60 + t->tm_min;
    bool       weekend = (t->tm_wday == 0 || t->tm_wday == 6);

    // Convert a minute-of-day to a bar x-coordinate (0..W)
    // Uses integer arithmetic to avoid float on ESP32 hot path
    auto xAt = [](int m) -> int { return (int)((long)m * W / MKT_DAY_MINS); };

    // ── Segment bar (top, 9px) ────────────────────────────────────────────────
    const int by = PRG_Y + 1;
    const int bh = 9;

    // Each segment: [start_min, end_min, dim_color, active_color]
    struct Seg { int s, e; uint32_t dim, bright; } segs[5] = {
        { 0,                MKT_PRE_START,  0x0E1520, 0x0E1520 },  // closed AM
        { MKT_PRE_START,  MKT_OPEN_START,  0x2E1A00, 0xFFAA00 },  // pre-market
        { MKT_OPEN_START,   MKT_OPEN_END,  0x002E10, 0x00CC44 },  // open
        { MKT_OPEN_END,    MKT_POST_END,   0x101E33, 0x3399CC },  // after-hours
        { MKT_POST_END,   MKT_DAY_MINS,   0x0E1520, 0x0E1520 },  // closed PM
    };

    for (int i = 0; i < 5; i++) {
        int      x1     = xAt(segs[i].s);
        int      x2     = (i == 4) ? W : xAt(segs[i].e);
        bool     active = !weekend && curMin >= segs[i].s && curMin < segs[i].e;
        uint32_t color  = active ? segs[i].bright : segs[i].dim;
        lcd.fillRect(x1, by, x2 - x1, bh, color);
    }

    // Phase boundary tick marks
    int bounds[4] = { MKT_PRE_START, MKT_OPEN_START, MKT_OPEN_END, MKT_POST_END };
    for (int i = 0; i < 4; i++)
        lcd.drawFastVLine(xAt(bounds[i]), by, bh, 0x2A4A6A);

    // Current-time cursor (bright white)
    if (!weekend) {
        int cx = xAt(curMin);
        if (cx > 0 && cx < W - 1)
            lcd.drawFastVLine(cx, by - 1, bh + 2, 0xFFFFFF);
    }

    // ── Status label (bottom, Font2) ──────────────────────────────────────────
    char     label[32];
    uint32_t labelColor;

    auto fmt = [](char* buf, const char* phase, int minsLeft) {
        if (minsLeft >= 60)
            snprintf(buf, 32, "%s  %dh %02dm left", phase,
                     minsLeft / 60, minsLeft % 60);
        else
            snprintf(buf, 32, "%s  %dm left", phase, minsLeft);
    };

    if (weekend) {
        snprintf(label, sizeof(label), "CLOSED  weekend");
        labelColor = C_MUTED;
    } else if (curMin < MKT_PRE_START) {
        fmt(label, "CLOSED", MKT_PRE_START - curMin);
        labelColor = C_MUTED;
    } else if (curMin < MKT_OPEN_START) {
        fmt(label, "PRE-MKT", MKT_OPEN_START - curMin);
        labelColor = 0xFFAA00;
    } else if (curMin < MKT_OPEN_END) {
        fmt(label, "OPEN", MKT_OPEN_END - curMin);
        labelColor = C_UP;
    } else if (curMin < MKT_POST_END) {
        fmt(label, "AFTER HRS", MKT_POST_END - curMin);
        labelColor = 0x3399CC;
    } else {
        fmt(label, "CLOSED", MKT_DAY_MINS - curMin + MKT_PRE_START);
        labelColor = C_MUTED;
    }

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(labelColor, C_BG);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(label, W / 2, PRG_Y + 12);
}

void drawAll() {
    lcd.fillScreen(C_BG);
    drawHeader();
    drawAllMarkets();
    drawProgress();
    drawDividers();
}
