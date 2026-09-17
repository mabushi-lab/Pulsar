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

// ── View mode ─────────────────────────────────────────────────────────────────
static bool s_silverView = false;
static const int SILVER_IDX = SILVER_MARKET_IDX;

// ── Refresh countdown ─────────────────────────────────────────────────────────
static int s_refreshCountdown = -1;
void displaySetRefreshCountdown(int secs) { s_refreshCountdown = secs; }

void displaySetSilverView(bool silver) { s_silverView = silver; }
bool displayIsSilverView()             { return s_silverView; }

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
    lcd.setRotation(DISPLAY_ROTATION);
    lcd.setBrightness(255);
}

void displayCycleBrightness() {
    static const uint8_t levels[] = { 255, 70, 0 };
    static int lvl = 0;
    lvl = (lvl + 1) % 3;
    lcd.setBrightness(levels[lvl]);
}

// ── Boot animations ───────────────────────────────────────────────────────────

// Typewriter title + subtitle. Called before the Wi-Fi association starts.
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

// Staggered panel reveal. Called once the link is up and the server is listening.
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

    // Panels slide in one by one (or single Silver panel)
    if (s_silverView) {
        drawSilverOnly();
        drawDividers();
    } else {
        for (int i = 0; i < MARKET_COUNT; i++) {
            drawMarketPanel(i);
            drawDividers();
            delay(60);
        }
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

    if (s_silverView) {
        if (s_flashEnd[SILVER_IDX] > 0 && now >= s_flashEnd[SILVER_IDX]) {
            s_flashEnd[SILVER_IDX] = 0;
            drawSilverOnly();
            drawDividers();
        }
    } else {
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
}

// ── Section draws ─────────────────────────────────────────────────────────────

// Header redraws every second for the colon blink, so it must stay fast.
void drawHeader() {
    lcd.fillRect(0, 0, W, HDR_H, C_HDR);

    struct tm tmNow;
    const bool haveTime = localNow(&tmNow);
    const struct tm* t  = &tmNow;

    // Time: HH:MM with blinking colon (off on odd seconds)
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_left);

    char hBuf[3], mBuf[3];
    if (haveTime) {
        snprintf(hBuf, sizeof(hBuf), "%02d", t->tm_hour);
        snprintf(mBuf, sizeof(mBuf), "%02d", t->tm_min);
    } else {
        // Before the first SNTP reply, show --:-- rather than a confident 01:00.
        snprintf(hBuf, sizeof(hBuf), "--");
        snprintf(mBuf, sizeof(mBuf), "--");
    }

    int tx = 5;
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(hBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(hBuf);

    // Colon: draw in header-bg colour on odd seconds = effectively invisible
    lcd.setTextColor((!haveTime || t->tm_sec % 2 == 0) ? C_DATE : C_HDR, C_HDR);
    lcd.drawString(":", tx, HDR_H / 2);
    tx += lcd.textWidth(":");

    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(mBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(mBuf);

    char dateBuf[14];
    if (haveTime) strftime(dateBuf, sizeof(dateBuf), "  %a %b %d", t);
    else          snprintf(dateBuf, sizeof(dateBuf), "  syncing");
    lcd.drawString(dateBuf, tx, HDR_H / 2);

    // WiFi dot: breathe between dim and bright green over a 3s cycle
    uint32_t ms     = millis();
    uint32_t cycle  = ms % 3000;
    uint8_t  bright = (cycle < 1500)
        ? (uint8_t)((cycle * 210u) / 1500u)
        : (uint8_t)(((3000u - cycle) * 210u) / 1500u);
    uint32_t dotColor = wifiConnected()
        ? lerpColor(0x004422, C_WIFI_OK, (uint8_t)(45u + bright))
        : C_WIFI_ERR;
    lcd.fillCircle(W - 6, HDR_H / 2, 3, dotColor);

    // Weather — right-aligned before the dot.
    // A failed fetch used to draw nothing at all, which is indistinguishable
    // from "this build has no weather"; show a muted placeholder instead.
    if (wxCode >= 0) {
        char wx[22];
        // '\xB0' (degree) is outside LovyanGFX bitmap font range → renders as square.
        // Use plain 'C' — clean and readable at small header size.
        snprintf(wx, sizeof(wx), "%.1fC  %s", tempC, wmoDesc(wxCode));
        lcd.setTextColor(C_WEATHER, C_HDR);
        lcd.setTextDatum(lgfx::middle_right);
        lcd.drawString(wx, W - 14, HDR_H / 2);
    } else if (wxFetched) {
        lcd.setTextColor(C_MUTED, C_HDR);
        lcd.setTextDatum(lgfx::middle_right);
        lcd.drawString("wx --", W - 14, HDR_H / 2);
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
    // "Offline" only when nothing was ever fetched. A later failure keeps the
    // last good price and dims it, so one rate-limited request no longer wipes
    // the panel of information it already has.
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
    lcd.setTextColor(m.stale ? C_DATE : C_PRICE, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, y + 18);

    // Change % (Font2, at y+46)
    char changeBuf[10];
    snprintf(changeBuf, sizeof(changeBuf), "%+.2f%%", m.changePct);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(m.stale ? C_MUTED : (m.changePct >= 0 ? C_UP : C_DOWN), C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(changeBuf, cx, y + 46);

    // A single dim pixel-dot marks the panel as showing a last-known value.
    if (m.stale) lcd.fillCircle(x + w - 5, y + 5, 2, C_MUTED);
}

void drawAllMarkets() {
    for (int i = 0; i < MARKET_COUNT; i++) drawMarketPanel(i);
}

// Market session bar, rendered by sweeping phaseAtMinute() across the day.
// It follows whichever instrument is on screen: the shared Euronext session in
// the 6-market grid, and the single instrument's own session in the large view.
static uint32_t phaseColor(MarketPhase p, bool active) {
    switch (p) {
        case PHASE_PRE:    return active ? 0xFFAA00 : 0x2E1A00;
        case PHASE_OPEN:   return active ? 0x00CC44 : 0x002E10;
        case PHASE_POST:   return active ? 0x3399CC : 0x101E33;
        case PHASE_CLOSED:
        default:           return 0x0E1520;
    }
}

static const char* phaseName(MarketPhase p) {
    switch (p) {
        case PHASE_PRE:  return "PRE-MKT";
        case PHASE_OPEN: return "OPEN";
        case PHASE_POST: return "AFTER HRS";
        default:         return "CLOSED";
    }
}

const TradingSession& displayedSession() {
    return (s_silverView && markets[SILVER_IDX].session)
        ? *markets[SILVER_IDX].session
        : SESSION_EQUITY;
}

void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    struct tm t;
    if (!localNow(&t)) {
        // No clock yet — a progress bar drawn from epoch 0 would be a lie.
        lcd.setFont(&fonts::Font2);
        lcd.setTextSize(1);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::top_center);
        lcd.drawString("waiting for time sync", W / 2, PRG_Y + 9);
        return;
    }

    const TradingSession& sess = displayedSession();
    const int  wday   = t.tm_wday;
    const int  curMin = t.tm_hour * 60 + t.tm_min;
    const MarketPhase curPhase = phaseAtMinute(sess, wday, curMin);

    const int by = PRG_Y;
    const int bh = 8;
    auto xAt = [](int m) -> int { return (int)((long)m * W / MKT_DAY_MINS); };

    // Sweep the day in runs of equal phase, so a 4-phase exchange day and a
    // 24/5 session with a maintenance break both fall out of the same code.
    int         runStart = 0;
    MarketPhase runPhase = phaseAtMinute(sess, wday, 0);
    bool        allClosed = true;

    for (int m = 1; m <= MKT_DAY_MINS; m++) {
        const MarketPhase p = (m < MKT_DAY_MINS) ? phaseAtMinute(sess, wday, m) : PHASE_CLOSED;
        if (m == MKT_DAY_MINS || p != runPhase) {
            const int  x1     = xAt(runStart);
            const int  x2     = (m == MKT_DAY_MINS) ? W : xAt(m);
            const bool active = (curMin >= runStart && curMin < m);
            if (x2 > x1) lcd.fillRect(x1, by, x2 - x1, bh, phaseColor(runPhase, active));
            if (runPhase != PHASE_CLOSED) allClosed = false;
            if (runStart > 0) lcd.drawFastVLine(x1, by, bh, 0x2A4A6A);   // phase boundary
            runStart = m;
            runPhase = p;
        }
    }

    // Glow cursor: flanking pixels dimmer, centre pixel bright white
    {
        const int cx = xAt(curMin);
        if (cx > 1 && cx < W - 2) {
            lcd.drawFastVLine(cx - 1, by, bh, 0x224433);
            lcd.drawFastVLine(cx,     by, bh, 0xFFFFFF);
            lcd.drawFastVLine(cx + 1, by, bh, 0x224433);
        }
    }

    // Minutes until the phase changes. Scans forward through the week so a
    // Sunday-evening open or a Friday close is counted correctly rather than
    // being clamped at midnight.
    int minsLeft = 0;
    {
        int w = wday, m = curMin;
        const int limit = 8 * MKT_DAY_MINS;   // a week and a day is always enough
        while (minsLeft < limit) {
            if (++m >= MKT_DAY_MINS) { m = 0; w = (w + 1) % 7; }
            minsLeft++;
            if (phaseAtMinute(sess, w, m) != curPhase) break;
        }
    }

    char     label[40];
    uint32_t labelColor;
    switch (curPhase) {
        case PHASE_PRE:  labelColor = 0xFFAA00; break;
        case PHASE_OPEN: labelColor = C_UP;     break;
        case PHASE_POST: labelColor = 0x3399CC; break;
        default:         labelColor = C_MUTED;  break;
    }

    // In the single-instrument view the bar describes one venue, so name it —
    // otherwise a Euronext timeline under a silver price reads as a claim about
    // silver itself.
    char prefix[12] = "";
    if (s_silverView) snprintf(prefix, sizeof(prefix), "%s  ", sess.label);

    if (allClosed) {
        snprintf(label, sizeof(label), "%s%s  weekend", prefix, phaseName(curPhase));
    } else if (minsLeft >= 60) {
        snprintf(label, sizeof(label), "%s%s  %dh %02dm left",
                 prefix, phaseName(curPhase), minsLeft / 60, minsLeft % 60);
    } else {
        snprintf(label, sizeof(label), "%s%s  %dm left",
                 prefix, phaseName(curPhase), minsLeft);
    }

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(labelColor, C_BG);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(label, W / 2, PRG_Y + 9);

    // Refresh countdown — right-aligned, muted
    if (s_refreshCountdown >= 0) {
        char cbuf[8];
        snprintf(cbuf, sizeof(cbuf), "~%ds", s_refreshCountdown);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::top_right);
        lcd.drawString(cbuf, W - 4, PRG_Y + 9);
    }
}

// ── Structural ────────────────────────────────────────────────────────────────
void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    if (!s_silverView) {
        lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
        lcd.drawFastVLine(107, ROW1_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(212, ROW1_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(107, ROW2_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(212, ROW2_Y, ROW_H, C_DIV);
    }
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

    // Big countdown (Font7, centred at y=70).
    // Clamped so a bad caller can never overflow the MM:SS field.
    if (secsLeft < 0) secsLeft = 0;
    int mm = secsLeft / 60; if (mm > 99) mm = 99;
    int ss = secsLeft % 60;
    char tbuf[8];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", mm, ss);
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

void drawSilverOnly() {
    const MarketItem& m  = markets[SILVER_IDX];
    const int         y  = ROW1_Y;
    const int         h  = DIV3_Y - y;   // 127px
    const int         cx = W / 2;

    // Open background — C_BG, not C_PANEL, so there's no "box" to see
    lcd.fillRect(0, y, W, h, C_BG);

    uint32_t now      = millis();
    bool     flashing = (s_flashEnd[SILVER_IDX] > 0 && now < s_flashEnd[SILVER_IDX]);
    if (flashing) {
        uint32_t elapsed = now - s_flashStart[SILVER_IDX];
        uint8_t  t       = (uint8_t)(255u - (elapsed * 255u) / FLASH_MS);
        lcd.fillRect(0, y, W, h, lerpColor(C_BG, 0x0D1F33, t));
    }

    // Accent bar at the very top edge
    lcd.fillRect(0, y, W, 3, m.accentColor);

    // Instrument name — Font4 gives it headline weight
    lcd.setFont(&fonts::Font4);
    lcd.setTextSize(1);
    lcd.setTextColor(m.accentColor, C_BG);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(m.label, cx, y + 5);

    if (!m.fetched) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Loading...", cx, y + h / 2);
        return;
    }
    if (!m.ok) {
        lcd.setTextColor(C_DOWN, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Offline", cx, y + h / 2);
        return;
    }

    // Thin rule below the label
    lcd.drawFastHLine(cx - 80, y + 34, 160, C_DIV);

    // Price — Font7, dominant (dimmed when showing a last-known value)
    char priceBuf[12];
    fmtPrice(priceBuf, sizeof(priceBuf), m.price);
    lcd.setFont(&fonts::Font7);
    lcd.setTextColor(m.stale ? C_DATE : C_PRICE, C_BG);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.drawString(priceBuf, cx, y + 66);

    // Change % — Font4, coloured, bottom of the area
    char changeBuf[10];
    snprintf(changeBuf, sizeof(changeBuf), "%+.2f%%", m.changePct);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(m.stale ? C_MUTED : (m.changePct >= 0 ? C_UP : C_DOWN), C_BG);
    lcd.setTextDatum(lgfx::bottom_center);
    lcd.drawString(changeBuf, cx, y + h - 6);
}
