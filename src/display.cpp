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

// ── Animation state ───────────────────────────────────────────────────────────
static const uint32_t FLASH_MS  = 380;
static uint32_t       s_flashStart[MARKET_COUNT] = {};
static uint32_t       s_flashEnd[MARKET_COUNT]   = {};

// Integer colour lerp: t=0 → a, t=255 → b
static uint32_t lerpColor(uint32_t a, uint32_t b, uint8_t t) {
    uint8_t r = (uint8_t)(((uint16_t)((a>>16)&0xFF)*(255u-t) + (uint16_t)((b>>16)&0xFF)*t) >> 8);
    uint8_t g = (uint8_t)(((uint16_t)((a>> 8)&0xFF)*(255u-t) + (uint16_t)((b>> 8)&0xFF)*t) >> 8);
    uint8_t bl= (uint8_t)(((uint16_t)((a    )&0xFF)*(255u-t) + (uint16_t)((b    )&0xFF)*t) >> 8);
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|bl;
}

// ── Public display control ────────────────────────────────────────────────────
void displayInit() {
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);
}

void displaySetBrightness(bool full) {
    lcd.setBrightness(full ? 255 : 60);
}

void displayCycleBrightness() {
    static const uint8_t levels[] = { 255, 70, 0 };
    static int lvl = 0;
    lvl = (lvl + 1) % 3;
    lcd.setBrightness(levels[lvl]);
}

// ── Boot animations ───────────────────────────────────────────────────────────

// Typewriter title + subtitle. Called before connectWiFi().
void animSplash() {
    lcd.fillScreen(C_BG);

    const int titleY = H / 2 - 16;
    const int subY   = H / 2 + 16;
    const int noteY  = H / 2 + 34;

    // "PULSAR" — each letter types in one at a time
    lcd.setFont(&fonts::Font4);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);

    const char* title = "PULSAR";
    for (int i = 1; i <= 6; i++) {
        lcd.fillRect(0, titleY - 15, W, 32, C_BG);
        char part[7] = {};
        strncpy(part, title, i);
        lcd.setTextColor(0x33CCFF, C_BG);
        lcd.drawString(part, W / 2, titleY);
        delay(85);
    }

    delay(160);

    // Subtitle fades in word by word
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString("MARKET", W / 2 - 28, subY);
    delay(80);
    lcd.drawString("MARKET DASHBOARD", W / 2, subY);
    delay(200);

    // Connecting note
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("Connecting to WiFi...", W / 2, noteY);
}

// Staggered panel reveal. Called after connectWiFi() + setupServer().
void animRevealMain() {
    // Quick flash to signal transition
    lcd.fillScreen(0x0F1E30);
    delay(35);
    lcd.fillScreen(C_BG);
    delay(25);

    // Header + dividers first
    drawDividers();
    drawHeader();
    delay(45);

    // Panels slide in one by one, left-to-right, row by row
    for (int i = 0; i < MARKET_COUNT; i++) {
        drawMarketPanel(i);
        drawDividers();
        delay(60);
    }

    drawProgress();
}

// ── Runtime animation helpers ─────────────────────────────────────────────────

void triggerPanelFlash(int idx) {
    s_flashStart[idx] = millis();
    s_flashEnd[idx]   = millis() + FLASH_MS;
}

// Call every loop: clears flash once it expires and redraws panels normally.
void animTick() {
    static uint32_t lastTick = 0;
    uint32_t now = millis();
    if (now - lastTick < 32) return;  // ~30 fps cap
    lastTick = now;

    bool needDividers = false;
    for (int i = 0; i < MARKET_COUNT; i++) {
        if (s_flashEnd[i] > 0 && now >= s_flashEnd[i]) {
            s_flashEnd[i] = 0;
            drawMarketPanel(i);
            needDividers = true;
        }
    }
    if (needDividers) drawDividers();
}

// ── Section draws ─────────────────────────────────────────────────────────────

// Header redraws every second for the colon blink, so it must stay fast.
void drawHeader() {
    lcd.fillRect(0, 0, W, HDR_H, C_HDR);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);

    // Time: HH:MM with blinking colon (off on odd seconds)
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_left);

    char hBuf[3], mBuf[3];
    snprintf(hBuf, sizeof(hBuf), "%02d", t->tm_hour);
    snprintf(mBuf, sizeof(mBuf), "%02d", t->tm_min);

    int tx = 5;
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(hBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(hBuf);

    // Colon: draw in header-bg colour on odd seconds = effectively invisible
    lcd.setTextColor((t->tm_sec % 2 == 0) ? C_DATE : C_HDR, C_HDR);
    lcd.drawString(":", tx, HDR_H / 2);
    tx += lcd.textWidth(":");

    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(mBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(mBuf);

    char dateBuf[14];
    strftime(dateBuf, sizeof(dateBuf), "  %a %b %d", t);
    lcd.drawString(dateBuf, tx, HDR_H / 2);

    // WiFi dot: breathe between dim and bright green over a 3s cycle
    uint32_t ms     = millis();
    uint32_t cycle  = ms % 3000;
    uint8_t  bright = (cycle < 1500)
        ? (uint8_t)((cycle * 210u) / 1500u)
        : (uint8_t)(((3000u - cycle) * 210u) / 1500u);
    uint32_t dotColor = (WiFi.status() == WL_CONNECTED)
        ? lerpColor(0x004422, C_WIFI_OK, (uint8_t)(45u + bright))
        : C_WIFI_ERR;
    lcd.fillCircle(W - 6, HDR_H / 2, 3, dotColor);

    // Weather — right-aligned before the dot
    if (wxCode >= 0) {
        char wx[22];
        // '\xB0' (degree) is outside LovyanGFX bitmap font range → renders as square.
        // Use plain 'C' — clean and readable at small header size.
        snprintf(wx, sizeof(wx), "%.1fC  %s", tempC, wmoDesc(wxCode));
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

    // Flash: slightly brighter bg + accent border, fading out over FLASH_MS
    uint32_t now      = millis();
    bool     flashing = (s_flashEnd[idx] > 0 && now < s_flashEnd[idx]);

    if (flashing) {
        uint32_t elapsed = now - s_flashStart[idx];
        uint8_t  t       = (uint8_t)(255u - (elapsed * 255u) / FLASH_MS);  // 255→0
        uint32_t bgColor = lerpColor(C_PANEL, 0x112238, t);
        lcd.fillRect(x, y, w, ROW_H, bgColor);
        // Glowing border that fades out with the flash
        uint32_t borderColor = lerpColor(C_PANEL, 0x2A5A8A, t);
        lcd.drawRect(x, y, w, ROW_H, borderColor);
    } else {
        lcd.fillRect(x, y, w, ROW_H, C_PANEL);
    }

    // Label (Font2 ≈ 14px, top margin 2px)
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(m.accentColor, flashing ? lerpColor(C_PANEL, 0x112238, 128) : C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(m.label, cx, y + 2);

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

    // Price (Font4 ≈ 26px, at y+18)
    char priceBuf[12];
    fmtPrice(priceBuf, sizeof(priceBuf), m.price);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_PRICE, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, y + 18);

    // Change % (Font2, at y+46)
    char changeBuf[10];
    snprintf(changeBuf, sizeof(changeBuf), "%+.2f%%", m.changePct);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(m.changePct >= 0 ? C_UP : C_DOWN, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(changeBuf, cx, y + 46);
}

void drawAllMarkets() {
    for (int i = 0; i < MARKET_COUNT; i++) drawMarketPanel(i);
}

// 4-phase market session bar (Euronext / Xetra, CET/CEST).
void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    time_t     epoch   = ntp.getEpochTime();
    struct tm *t       = localtime(&epoch);
    int        curMin  = t->tm_hour * 60 + t->tm_min;
    bool       weekend = (t->tm_wday == 0 || t->tm_wday == 6);

    auto xAt = [](int m) -> int { return (int)((long)m * W / MKT_DAY_MINS); };

    const int by = PRG_Y;
    const int bh = 8;

    struct Seg { int s, e; uint32_t dim, bright; } segs[5] = {
        { 0,              MKT_PRE_START,  0x0E1520, 0x0E1520 },
        { MKT_PRE_START,  MKT_OPEN_START, 0x2E1A00, 0xFFAA00 },
        { MKT_OPEN_START, MKT_OPEN_END,   0x002E10, 0x00CC44 },
        { MKT_OPEN_END,   MKT_POST_END,   0x101E33, 0x3399CC },
        { MKT_POST_END,   MKT_DAY_MINS,   0x0E1520, 0x0E1520 },
    };

    for (int i = 0; i < 5; i++) {
        int      x1     = xAt(segs[i].s);
        int      x2     = (i == 4) ? W : xAt(segs[i].e);
        bool     active = !weekend && curMin >= segs[i].s && curMin < segs[i].e;
        lcd.fillRect(x1, by, x2 - x1, bh, active ? segs[i].bright : segs[i].dim);
    }

    // Phase boundary marks
    int bounds[4] = { MKT_PRE_START, MKT_OPEN_START, MKT_OPEN_END, MKT_POST_END };
    for (int i = 0; i < 4; i++)
        lcd.drawFastVLine(xAt(bounds[i]), by, bh, 0x2A4A6A);

    // Glow cursor: flanking pixels dimmer, centre pixel bright white
    if (!weekend) {
        int cx = xAt(curMin);
        if (cx > 1 && cx < W - 2) {
            lcd.drawFastVLine(cx - 1, by, bh, 0x224433);
            lcd.drawFastVLine(cx,     by, bh, 0xFFFFFF);
            lcd.drawFastVLine(cx + 1, by, bh, 0x224433);
        }
    }

    // Status label (Font2, at PRG_Y+9 — fits exactly to screen bottom)
    char     label[32];
    uint32_t labelColor;

    auto fmt = [](char* buf, const char* phase, int minsLeft) {
        if (minsLeft >= 60)
            snprintf(buf, 32, "%s  %dh %02dm left", phase, minsLeft/60, minsLeft%60);
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
    lcd.drawString(label, W / 2, PRG_Y + 9);
}

// ── Structural ────────────────────────────────────────────────────────────────
void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    lcd.drawFastVLine(107, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(107, ROW2_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW2_Y, ROW_H, C_DIV);
}

// ── Pomodoro screen ───────────────────────────────────────────────────────────
// state: 0=idle, 1=running, 2=paused
void drawPomodoro(int secsLeft, int totalSecs, bool isWork, int state, int sessions) {
    lcd.fillScreen(C_BG);

    // Phase progress bar across the very top (8px)
    uint32_t barColor = isWork ? 0x00CC44u : 0x3399CCu;
    lcd.fillRect(0, 0, W, 8, C_PANEL);
    if (state > 0 && totalSecs > 0) {
        int filled = (int)((long)(totalSecs - secsLeft) * W / totalSecs);
        if (filled > 0) lcd.fillRect(0, 0, filled, 8, barColor);
    }

    // Big countdown (Font7, centred at y=70)
    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", secsLeft / 60, secsLeft % 60);
    lcd.setFont(&fonts::Font7);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(state == 2 ? (uint32_t)0x445566 : (uint32_t)0xEEF2FF, C_BG);
    lcd.drawString(tbuf, W / 2, 70);

    // Phase / state label (Font4, centred at y=115)
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(lgfx::middle_center);
    if (state == 0) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("READY", W / 2, 115);
    } else if (state == 2) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("PAUSED", W / 2, 115);
    } else if (isWork) {
        lcd.setTextColor(C_UP, C_BG);
        lcd.drawString("WORK", W / 2, 115);
    } else {
        lcd.setTextColor(0x3399CCu, C_BG);
        lcd.drawString("BREAK", W / 2, 115);
    }

    // Session counter (Font2, y=145)
    char sbuf[20];
    snprintf(sbuf, sizeof(sbuf), "session %d", sessions + 1);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.drawString(sbuf, W / 2, 145);

    // Hint (Font2, y=160)
    const char* hint =
        (state == 0) ? "tap: start    hold: exit" :
        (state == 2) ? "tap: resume   hold: exit" :
                       "tap: pause    hold: exit";
    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString(hint, W / 2, 160);
}

// Brief colour flash when a phase ends — signals the user without a buzzer
void animPomAlert(bool wasWork) {
    uint32_t color = wasWork ? 0x003322u : 0x002233u;
    for (int i = 0; i < 3; i++) {
        lcd.fillScreen(color);
        delay(120);
        lcd.fillScreen(C_BG);
        delay(80);
    }
}

void drawAll() {
    lcd.fillScreen(C_BG);
    drawHeader();
    drawAllMarkets();
    drawProgress();
    drawDividers();
}
