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

void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);
    int        secs  = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    int        pct   = (int)((long)secs * 100 / 86400);

    const int PAD = 4, LW = 26, PW = 34;
    const int bx  = PAD + LW + 3;
    const int bw  = W - bx - PW - PAD;
    const int by  = PRG_Y + (PRG_H - 8) / 2;
    const int bh  = 8;

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::middle_right);
    lcd.drawString("DAY", PAD + LW, by + bh / 2);

    lcd.fillRoundRect(bx, by, bw, bh, 3, C_PANEL);

    int      fw = (bw * pct) / 100;
    uint32_t fc = (pct < 50) ? C_WIFI_OK : (pct < 80) ? 0xFFAA00u : 0xFF4400u;
    if (fw > 3) lcd.fillRoundRect(bx, by, fw, bh, 3, fc);

    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.setTextDatum(lgfx::middle_left);
    lcd.drawString(buf, bx + bw + PAD, by + bh / 2);
}

void drawAll() {
    lcd.fillScreen(C_BG);
    drawHeader();
    drawAllMarkets();
    drawProgress();
    drawDividers();
}
