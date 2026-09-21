#include "display.h"
#include "config.h"
#include "data.h"
#include "portfolio.h"
#include "network.h"
#include "settings.h"
#include "loan.h"
#include "history.h"
#include <LovyanGFX.hpp>
#include <string.h>

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

// ── State ─────────────────────────────────────────────────────────────────────
static const uint32_t FLASH_MS = 380;
static uint32_t s_flashStart[GRID_SLOTS] = {};
static uint32_t s_flashEnd[GRID_SLOTS]   = {};

static DisplayView s_view = VIEW_POSITIONS;
static int         s_page = 0;
static int         s_detail = 0;
static uint32_t    s_revealUntil = 0;
static int         s_refreshCountdown = -1;

void displaySetRefreshCountdown(int secs) {
    if (secs < 0)    secs = -1;
    if (secs > 9999) secs = 9999;
    s_refreshCountdown = secs;
}

int displayDetailIndex() { return s_detail; }

static int displayPageCount() {
    if (positionCount <= 0) return 1;
    return (positionCount + GRID_SLOTS - 1) / GRID_SLOTS;
}

int displaySlotForPosition(int posIndex) {
    const int base = s_page * GRID_SLOTS;
    if (posIndex < base || posIndex >= base + GRID_SLOTS) return -1;
    return posIndex - base;
}

// Deleting positions can leave s_page past the last page (blank grid) or
// s_detail past the end. Clamped wherever they are read rather than only on
// edit, so no path can miss it.
static void clampIndices() {
    const int pages = displayPageCount();
    if (s_page < 0 || s_page >= pages) s_page = 0;
    if (s_detail < 0 || s_detail >= positionCount) s_detail = 0;
}

static int positionForSlot(int slot) {
    const int idx = s_page * GRID_SLOTS + slot;
    return (idx < positionCount) ? idx : -1;
}

// "Are amounts on screen right now?" — always in mode 2, never in mode 0, and
// for a few seconds after a USER tap in mode 1.
static bool portfolioRevealActive() {
    if (settings.amountMode == 0) return false;
    if (settings.amountMode == 2) return true;
    return s_revealUntil != 0 && millis() < s_revealUntil;
}
static void drawPositions();      // used by displayRefreshAll, defined below

static void portfolioReveal() {
    if (settings.amountMode == 1) s_revealUntil = millis() + PORTFOLIO_REVEAL_MS;
}

void displaySetView(DisplayView v) { s_view = v; s_revealUntil = 0; }
DisplayView displayView()          { return s_view; }

// BOOT cycles subjects, not screens: the instruments, one instrument, the whole
// book, then the borrowed sleeve. Allocation is reached from Loan with USER,
// because both describe the same five funds and cycling past one to reach the
// other treated them as unrelated.
static const DisplayView CYCLE[] = { VIEW_POSITIONS, VIEW_DETAIL, VIEW_PORTFOLIO, VIEW_LOAN };
static const int CYCLE_LEN = (int)(sizeof(CYCLE) / sizeof(CYCLE[0]));

void displayNextView() {
    // Allocation shares the loan's slot, so leaving it moves on from there
    // rather than dropping back to the start.
    const DisplayView here = (s_view == VIEW_ALLOC) ? VIEW_LOAN : s_view;
    int at = 0;
    for (int i = 0; i < CYCLE_LEN; i++) if (CYCLE[i] == here) { at = i; break; }
    s_view = CYCLE[(at + 1) % CYCLE_LEN];
    s_revealUntil = 0;   // never carry a reveal across a view change
}

// One button, one meaning per view: "show me the next thing".
void displayUserAction() {
    clampIndices();
    switch (s_view) {
        case VIEW_POSITIONS:
            if (displayPageCount() > 1) s_page = (s_page + 1) % displayPageCount();
            break;
        case VIEW_DETAIL:
            if (positionCount > 0) s_detail = (s_detail + 1) % positionCount;
            break;
        case VIEW_PORTFOLIO:
            portfolioReveal();
            break;
        case VIEW_ALLOC:
            // The two faces of the sleeve: what it owes, and how it is balanced.
            s_view = VIEW_LOAN;
            portfolioReveal();
            break;
        case VIEW_LOAN:
            s_view = VIEW_ALLOC;
            portfolioReveal();
            break;
    }
}

static void portfolioRevealTick() {
    if (s_revealUntil != 0 && millis() >= s_revealUntil) {
        s_revealUntil = 0;
        if (s_view == VIEW_PORTFOLIO) { drawPortfolio(); drawDividers(); }
        if (s_view == VIEW_LOAN)      { drawLoan();      drawDividers(); }
        if (s_view == VIEW_ALLOC)     { drawAllocation(); drawDividers(); }
    }
}

// Integer colour lerp: t=0 → a, t=255 → b
static uint32_t lerpColor(uint32_t a, uint32_t b, uint8_t t) {
    uint8_t r = (uint8_t)(((uint16_t)((a>>16)&0xFF)*(255u-t) + (uint16_t)((b>>16)&0xFF)*t) >> 8);
    uint8_t g = (uint8_t)(((uint16_t)((a>> 8)&0xFF)*(255u-t) + (uint16_t)((b>> 8)&0xFF)*t) >> 8);
    uint8_t bl= (uint8_t)(((uint16_t)((a    )&0xFF)*(255u-t) + (uint16_t)((b    )&0xFF)*t) >> 8);
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|bl;
}

// ── Display control ───────────────────────────────────────────────────────────
void displayInit() {
    lcd.init();
    lcd.setRotation(settings.rotation);
    lcd.setBrightness(settings.brightness);
}

void displayApplyRotation() {
    lcd.setRotation(settings.rotation);
    lcd.fillScreen(C_BG);
}

// ── Night dimming ─────────────────────────────────────────────────────────────
// s_lastAuto is the level the SCHEDULE last asked for, not the level the panel
// is at. Comparing against it means a manual brightness press is respected
// until the next dusk or dawn, instead of being undone a second later.
static int s_lastAuto = -1;

static uint8_t displayScheduledBrightness(int minuteOfDay) {
    if (!settings.nightDim) return settings.brightness;
    return settingsInNightWindow(minuteOfDay, settings.nightStartMin, settings.nightEndMin)
         ? settings.nightBright : settings.brightness;
}

void displayBrightnessTick() {
    struct tm t;
    if (!localNow(&t)) return;         // no clock yet: leave the panel alone
    const int want = displayScheduledBrightness(t.tm_hour * 60 + t.tm_min);
    if (want == s_lastAuto) return;
    s_lastAuto = want;
    lcd.setBrightness((uint8_t)want);
}

void displayApplyScheduledBrightness() {
    struct tm t;
    if (!localNow(&t)) { lcd.setBrightness(settings.brightness); s_lastAuto = -1; return; }
    s_lastAuto = displayScheduledBrightness(t.tm_hour * 60 + t.tm_min);
    lcd.setBrightness((uint8_t)s_lastAuto);
}

// A manual override. It does not touch s_lastAuto, so the schedule's next
// transition takes the panel back without a fight.
void displayCycleBrightness() {
    static const uint8_t levels[] = { 255, 70, 0 };
    static int lvl = 0;
    lvl = (lvl + 1) % 3;
    lcd.setBrightness(levels[lvl]);
}

void displayRefreshAll() {
    lcd.fillScreen(C_BG);
    drawHeader();
    switch (s_view) {
        case VIEW_DETAIL:    drawDetail();    break;
        case VIEW_PORTFOLIO: drawPortfolio(); break;
        case VIEW_ALLOC:     drawAllocation(); break;
        case VIEW_LOAN:      drawLoan();      break;
        default:             drawPositions(); break;
    }
    drawFooter();
    drawDividers();
}

// ── Boot screens ──────────────────────────────────────────────────────────────
void animSplash() {
    lcd.fillScreen(C_BG);
    const int titleY = H / 2 - 16;
    const int subY   = H / 2 + 16;
    const int noteY  = H / 2 + 34;

    lcd.setFont(&fonts::Font4);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);

    const char* title = "PULSAR";
    for (int i = 1; i <= 6; i++) {
        lcd.fillRect(0, titleY - 15, W, 32, C_BG);
        char part[7] = {};
        strncpy(part, title, i);
        lcd.setTextColor(C_ACCENT, C_BG);
        lcd.drawString(part, W / 2, titleY);
        delay(85);
    }
    delay(160);

    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString("PORTFOLIO DASHBOARD", W / 2, subY);
    delay(200);

    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("Connecting to WiFi...", W / 2, noteY);
}

void animShowAddress() {
    lcd.fillScreen(C_BG);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);

    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.drawString("CONFIGURE AT", W / 2, H / 2 - 44);

    if (!wifiConnected()) {
        lcd.setFont(&fonts::Font4);
        lcd.setTextColor(C_DOWN, C_BG);
        lcd.drawString("no Wi-Fi", W / 2, H / 2 - 8);
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("check credentials in secrets.h", W / 2, H / 2 + 24);
        delay(2500);
        return;
    }

    char host[48];
    snprintf(host, sizeof(host), "http://%s/", deviceHostname());
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_ACCENT, C_BG);
    lcd.drawString(host, W / 2, H / 2 - 12);

    char ip[56];
    snprintf(ip, sizeof(ip), "or  http://%s/", deviceAddress());
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString(ip, W / 2, H / 2 + 18);

    lcd.setTextColor(C_MUTED, C_BG);
    lcd.drawString("positions - settings - refresh", W / 2, H / 2 + 44);
    delay(3500);
}

// ── OTA screen ────────────────────────────────────────────────────────────────
// Drawn from the ArduinoOTA callbacks. It repaints only the bar and the number
// on each progress tick: a full-screen clear at 30 Hz during a flash would slow
// the transfer down and flicker the whole way through.
void drawOtaScreen(int pct, const char* note, bool error) {
    static bool framed = false;
    if (pct < 0) framed = false;

    const int barY = H / 2 + 6;
    const int barX = 30, barW = W - 60, barH = 14;

    if (!framed) {
        lcd.fillScreen(C_BG);
        lcd.setTextSize(1);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.setFont(&fonts::Font4);
        lcd.setTextColor(error ? C_DOWN : C_ACCENT, C_BG);
        lcd.drawString(error ? "UPDATE FAILED" : "FIRMWARE UPDATE", W / 2, H / 2 - 34);
        lcd.drawRect(barX, barY, barW, barH, C_DIV);
        framed = true;
    }

    if (pct >= 0) {
        const int fill = (barW - 4) * (pct > 100 ? 100 : pct) / 100;
        lcd.fillRect(barX + 2, barY + 2, fill, barH - 4, error ? C_DOWN : C_ACCENT);
        if (fill < barW - 4)
            lcd.fillRect(barX + 2 + fill, barY + 2, barW - 4 - fill, barH - 4, C_BG);
    }

    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    char line[48];
    if (pct >= 0) snprintf(line, sizeof(line), "%s  %d%%", note, pct);
    else          snprintf(line, sizeof(line), "%s", note);
    lcd.fillRect(0, barY + barH + 6, W, 18, C_BG);
    lcd.drawString(line, W / 2, barY + barH + 14);

    lcd.setTextColor(C_MUTED, C_BG);
    lcd.fillRect(0, H - 22, W, 18, C_BG);
    lcd.drawString(error ? "press RESET or reflash over USB" : "do not unplug", W / 2, H - 14);
}

void animRevealMain() {
    lcd.fillScreen(0x0F1E30);
    delay(35);
    lcd.fillScreen(C_BG);
    delay(25);

    drawDividers();
    drawHeader();
    delay(45);

    if (s_view == VIEW_POSITIONS) {
        for (int i = 0; i < GRID_SLOTS; i++) {
            drawPositionPanel(i);
            drawDividers();
            delay(50);
        }
    } else {
        displayRefreshAll();
        return;
    }
    drawFooter();
}

// ── Animations ────────────────────────────────────────────────────────────────
void triggerPanelFlash(int slot) {
    if (slot < 0 || slot >= GRID_SLOTS) return;
    s_flashStart[slot] = millis();
    s_flashEnd[slot]   = millis() + FLASH_MS;
}

void animTick() {
    static uint32_t lastTick = 0;
    const uint32_t now = millis();
    if (now - lastTick < 32) return;   // ~30 fps cap
    lastTick = now;

    if (s_view != VIEW_POSITIONS) { portfolioRevealTick(); return; }

    bool needDividers = false;
    for (int i = 0; i < GRID_SLOTS; i++) {
        if (s_flashEnd[i] > 0 && now >= s_flashEnd[i]) {
            s_flashEnd[i] = 0;
            drawPositionPanel(i);
            needDividers = true;
        }
    }
    if (needDividers) drawDividers();
}

// ── Header ────────────────────────────────────────────────────────────────────
// Clock, date, portfolio day change, link state. The day change lives here so
// the number you most want is on screen in every view.
void drawHeader() {
    lcd.fillRect(0, 0, W, HDR_H, C_HDR);

    struct tm tmNow;
    const bool haveTime = localNow(&tmNow);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_left);

    char hBuf[12], mBuf[12];
    if (haveTime) {
        snprintf(hBuf, sizeof(hBuf), "%02d", tmNow.tm_hour);
        snprintf(mBuf, sizeof(mBuf), "%02d", tmNow.tm_min);
    } else {
        snprintf(hBuf, sizeof(hBuf), "--");
        snprintf(mBuf, sizeof(mBuf), "--");
    }

    int tx = 5;
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(hBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(hBuf);
    lcd.setTextColor((!haveTime || tmNow.tm_sec % 2 == 0) ? C_DATE : C_HDR, C_HDR);
    lcd.drawString(":", tx, HDR_H / 2);
    tx += lcd.textWidth(":");
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.drawString(mBuf, tx, HDR_H / 2);
    tx += lcd.textWidth(mBuf);

    char dateBuf[14];
    if (haveTime) strftime(dateBuf, sizeof(dateBuf), "  %a %b %d", &tmNow);
    else          snprintf(dateBuf, sizeof(dateBuf), "  syncing");
    lcd.drawString(dateBuf, tx, HDR_H / 2);

    const uint32_t ms    = millis();
    const uint32_t cycle = ms % 3000;
    const uint8_t bright = (cycle < 1500) ? (uint8_t)((cycle * 210u) / 1500u)
                                          : (uint8_t)(((3000u - cycle) * 210u) / 1500u);
    lcd.fillCircle(W - 6, HDR_H / 2, 3,
                   wifiConnected() ? lerpColor(0x004422, C_WIFI_OK, (uint8_t)(45u + bright))
                                   : C_WIFI_ERR);

    const PortfolioTotals t = portfolioTotals();
    lcd.setTextDatum(lgfx::middle_right);
    if (t.valid) {
        char day[16];
        fmtPct(day, sizeof(day), t.dayChangePct, 2);
        lcd.setTextColor(t.anyStale ? C_DATE : (t.dayChangePct >= 0 ? C_UP : C_DOWN), C_HDR);
        lcd.drawString(day, W - 14, HDR_H / 2);
    } else {
        // Nothing priced yet reads identically to a broken header unless it
        // says so - a fresh device with only watchlist rows, or one that just
        // booted and has not completed its first fetch, would otherwise leave
        // this corner blank with no way to tell "empty" from "broken".
        lcd.setTextColor(C_MUTED, C_HDR);
        lcd.drawString("--", W - 14, HDR_H / 2);
    }
}

// ── Positions grid ────────────────────────────────────────────────────────────
void drawPositionPanel(int slot) {
    const int col = slot % 3;
    const int x   = COL_X[col];
    const int w   = COL_W[col];
    const int y   = (slot < 3) ? ROW1_Y : ROW2_Y;
    const int cx  = x + w / 2;

    const int idx = positionForSlot(slot);
    if (idx < 0) { lcd.fillRect(x, y, w, ROW_H, C_BG); return; }
    const Position& p = positions[idx];

    const uint32_t now = millis();
    const bool flashing = (s_flashEnd[slot] > 0 && now < s_flashEnd[slot]);
    // Text is drawn with an opaque background, so it has to match whatever the
    // panel itself was just filled with - during the flash that is a lerped
    // tint, not the resting C_PANEL, or every character sits in a mismatched
    // box for the ~380ms of every single price update.
    uint32_t panelBg = C_PANEL;
    if (flashing) {
        const uint32_t elapsed = now - s_flashStart[slot];
        const uint8_t t = (uint8_t)(255u - (elapsed * 255u) / FLASH_MS);
        panelBg = lerpColor(C_PANEL, 0x112238, t);
        lcd.fillRect(x, y, w, ROW_H, panelBg);
        lcd.drawRect(x, y, w, ROW_H, lerpColor(C_PANEL, 0x2A5A8A, t));
    } else {
        lcd.fillRect(x, y, w, ROW_H, C_PANEL);
    }

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(p.accent, panelBg);
    lcd.setTextDatum(lgfx::top_left);
    lcd.drawString(p.label, x + 4, y + 2);

    // A hollow dot marks a watchlist row, so "not mine" is visible at a glance.
    if (!positionIsHeld(p)) lcd.drawCircle(x + w - 7, y + 8, 3, C_LABEL);

    if (!p.fetched) {
        lcd.setTextColor(C_MUTED, panelBg);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("...", cx, y + ROW_H / 2);
        return;
    }
    if (!p.ok) {
        lcd.setTextColor(C_DOWN, panelBg);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("no price", cx, y + ROW_H / 2);
        return;
    }

    char priceBuf[14];
    fmtPrice(priceBuf, sizeof(priceBuf), p.price);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(p.stale ? C_DATE : C_PRICE, panelBg);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, y + 18);

    const double dayPct = positionDayPct(p);
    char changeBuf[12];
    fmtPct(changeBuf, sizeof(changeBuf), dayPct, 2);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(p.stale ? C_MUTED : (dayPct >= 0 ? C_UP : C_DOWN), panelBg);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(changeBuf, cx, y + 46);

    if (p.stale) lcd.fillCircle(x + 5, y + 8, 2, C_MUTED);
}

static void drawPositions() {
    clampIndices();
    if (positionCount == 0) {
        lcd.fillRect(0, ROW1_Y, W, DIV3_Y - ROW1_Y, C_BG);
        lcd.setFont(&fonts::Font2);
        lcd.setTextSize(1);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        char where[64];
        snprintf(where, sizeof(where), "add positions at http://%s/", deviceHostname());
        lcd.drawString(where, W / 2, H / 2 - 6);
        snprintf(where, sizeof(where), "or http://%s/", deviceAddress());
        lcd.setTextColor(C_LABEL, C_BG);
        lcd.drawString(where, W / 2, H / 2 + 12);
        return;
    }
    for (int i = 0; i < GRID_SLOTS; i++) drawPositionPanel(i);
}

// ── Detail ────────────────────────────────────────────────────────────────────
void drawDetail() {
    const int y  = ROW1_Y;
    const int h  = DIV3_Y - y;
    const int cx = W / 2;

    lcd.fillRect(0, y, W, h, C_BG);
    if (positionCount == 0) {
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("no positions", cx, y + h / 2);
        return;
    }
    clampIndices();
    const Position& p = positions[s_detail];

    lcd.fillRect(0, y, W, 3, p.accent);

    lcd.setFont(&fonts::Font4);
    lcd.setTextSize(1);
    lcd.setTextColor(p.accent, C_BG);
    lcd.setTextDatum(lgfx::top_left);
    lcd.drawString(p.label, 6, y + 5);

    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::top_right);
    char meta[48];
    snprintf(meta, sizeof(meta), "%d/%d  %s", s_detail + 1, positionCount, p.symbol);
    lcd.drawString(meta, W - 6, y + 8);

    if (!p.ok) {
        lcd.setTextColor(p.fetched ? C_DOWN : C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(p.fetched ? "no price from Yahoo" : "waiting for price...", cx, y + h / 2);
        return;
    }

    lcd.drawFastHLine(cx - 100, y + 34, 200, C_DIV);

    char priceRaw[14], priceBuf[16];
    fmtPrice(priceRaw, sizeof(priceRaw), p.price);
    fmtSevenSeg(priceBuf, sizeof(priceBuf), priceRaw);   // the face has no comma
    lcd.setFont(&fonts::Font7);
    lcd.setTextColor(p.stale ? C_DATE : C_PRICE, C_BG);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.drawString(priceBuf, cx, y + 62);

    const double dayPct = positionDayPct(p);
    char line[64], pctBuf[16];
    fmtPct(pctBuf, sizeof(pctBuf), dayPct, 2);
    // Per instrument, not portfolio-wide: this one's own venue decides whether
    // the figure is today's or the last session's. Stuttgart is still trading
    // at 19:00 when Xetra has been shut for ninety minutes.
    struct tm tmNow;
    const bool shutHere = p.session && localNow(&tmNow) &&
                          sessionPhase(*p.session, tmNow) == PHASE_CLOSED;
    snprintf(line, sizeof(line), "%s %s", pctBuf, shutHere ? "last close" : "today");
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(p.stale ? C_MUTED : (dayPct >= 0 ? C_UP : C_DOWN), C_BG);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.drawString(line, cx, y + 96);

    // Held positions get their own return; a watchlist row has no cost basis.
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(lgfx::middle_center);
    if (positionIsHeld(p)) {
        // Size first, then either allocation or return. Drift is a percentage,
        // not a private figure, so it must not depend on the amount mode -
        // making it conditional on `reveal` hid it entirely once amounts
        // became the default.
        const double ret = positionReturnPct(p);
        char lead[36];
        if (portfolioRevealActive()) {
            char money[28];
            fmtMoney(money, sizeof(money), positionValueBase(p));
            snprintf(lead, sizeof(lead), "%s %s", money, settings.baseCurrency);
        } else {
            snprintf(lead, sizeof(lead), "%.4g units", p.qty);
        }

        if (p.target > 0) {
            const PortfolioTotals tt = portfolioTotals();
            const double wgt   = positionWeightPct(p, tt);
            const double drift = positionDriftPct(p, tt);
            const double mag   = drift < 0 ? -drift : drift;
            char wgtBuf[16], driftBuf[16];
            fmtPct(wgtBuf,   sizeof(wgtBuf),   wgt,   1);
            fmtPct(driftBuf, sizeof(driftBuf), drift, 1);
            snprintf(line, sizeof(line), "%s   %s of %d%%  (%s)",
                     lead, wgtBuf + 1, (int)(p.target + 0.5), driftBuf);
            lcd.setTextColor(mag >= DRIFT_WARN_PCT ? 0xFFAA00 : C_LABEL, C_BG);
        } else {
            char retBuf[16];
            fmtPct(retBuf, sizeof(retBuf), ret, 1);
            snprintf(line, sizeof(line), "%s   %s total", lead, retBuf);
            lcd.setTextColor(ret >= 0 ? C_UP : C_DOWN, C_BG);
        }
    } else {
        snprintf(line, sizeof(line), "watchlist  %s", p.currency);
        lcd.setTextColor(C_LABEL, C_BG);
    }
    lcd.drawString(line, cx, y + h - 10);
}

// ── Portfolio summary ─────────────────────────────────────────────────────────
// Percentages by default, because this sits on a desk. Absolute amounts appear
// only for PORTFOLIO_REVEAL_MS after a deliberate USER tap.
void drawPortfolio() {
    const int y  = ROW1_Y;
    const int h  = DIV3_Y - y;
    const int cx = W / 2;

    lcd.fillRect(0, y, W, h, C_BG);
    lcd.fillRect(0, y, W, 3, C_ACCENT);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_ACCENT, C_BG);
    lcd.setTextDatum(lgfx::top_left);
    // The big figure below is a day change, and on a closed market that day is
    // not today. Saying so in the title covers both layouts at once.
    const bool shut = marketsAllClosed();
    lcd.drawString(shut ? "PORTFOLIO - LAST CLOSE" : "PORTFOLIO", 6, y + 6);

    const PortfolioTotals t = portfolioTotals();

    char meta[56];
    if (t.held == 0) {
        snprintf(meta, sizeof(meta), "no holdings");
    } else if (t.valid && t.priced == t.held && portfolioRevealActive()) {
        // Everything priced and amounts allowed: the total value is better use
        // of this corner than a "6/6" that is only interesting when it is not.
        char money[28];
        fmtMoney(money, sizeof(money), t.value);
        snprintf(meta, sizeof(meta), "%s %s", money, settings.baseCurrency);
    } else {
        snprintf(meta, sizeof(meta), "%d/%d priced  %s", t.priced, t.held, settings.baseCurrency);
    }
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::top_right);
    lcd.drawString(meta, W - 6, y + 6);

    if (t.held == 0) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("add a quantity to a position to track it", cx, y + h / 2);
        return;
    }
    if (!t.valid) {
        lcd.setTextDatum(lgfx::middle_center);
        if (t.unpriced > 0) {
            const char* bad = "";
            int code = 0;
            for (int i = 0; i < positionCount; i++) {
                if (positionIsHeld(positions[i]) && !positions[i].ok && positions[i].lastHttp != 0) {
                    bad = positions[i].symbol; code = positions[i].lastHttp; break;
                }
            }
            // GCC cannot see that `bad` is a char[SYMBOL_MAX] reached through an
            // array, so it warns here. A bounded copy silences nothing - it only
            // moves the same warning onto the copy - so the buffer stays simple.
            char msg[64];
            lcd.setTextColor(C_DOWN, C_BG);
            snprintf(msg, sizeof(msg), "no price for %s", bad);
            lcd.drawString(msg, cx, y + h / 2 - 10);
            lcd.setTextColor(C_MUTED, C_BG);
            snprintf(msg, sizeof(msg), "HTTP %d - check the symbol", code);
            lcd.drawString(msg, cx, y + h / 2 + 10);
        } else {
            lcd.setTextColor(C_MUTED, C_BG);
            lcd.drawString("waiting for prices...", cx, y + h / 2);
        }
        return;
    }

    const bool reveal = portfolioRevealActive();

    // The hero figure is total return, not the day change. Cost basis is a
    // constant, so value-minus-cost moves by exactly as much intraday as
    // value-minus-yesterday does - the screen loses no liveliness - but it is
    // measured against a baseline that still means something on a Saturday and
    // that a target allocation is actually judged by. The day change resets
    // every night, is frozen for about three quarters of the week, and on a
    // five-fund index portfolio never once implies an action.
    const uint32_t tone    = (t.totalReturnPct >= 0) ? C_UP : C_DOWN;
    const uint32_t dayTone = shut ? C_MUTED : C_DATE;   // context, not headline
    const char* dayWhen    = shut ? "last close" : "today";

    if (reveal) {
        // Today's move in money is the number you would actually react to; the
        // percentage is the scale-free context for it, so show both rather than
        // making one hide the other.
        char totM[28], heroRaw[40], hero[40];
        fmtMoney(totM, sizeof(totM), t.totalReturnAbs);
        snprintf(heroRaw, sizeof(heroRaw), "%s", totM);
        fmtSevenSeg(hero, sizeof(hero), heroRaw);
        lcd.setFont(&fonts::Font7);
        lcd.setTextColor(t.anyStale ? C_DATE : tone, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(hero, cx, y + 50);

        // The day change in money, because magnitude is the part a percentage
        // hides. Its percentage is on the header in every view, so repeating it
        // here would only cost the width that keeps this line inside 320px.
        char dayM[28], totPctBuf[16], line[104];
        fmtMoney(dayM, sizeof(dayM), t.dayChangeAbs);
        fmtPct(totPctBuf, sizeof(totPctBuf), t.totalReturnPct, 1);
        snprintf(line, sizeof(line), "%s return   %s %s%s",
                 totPctBuf, dayWhen, t.dayChangeAbs >= 0 ? "+" : "", dayM);
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(dayTone, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(line, cx, y + 90);
    } else {
        char heroRaw[24], hero[24];
        fmtPct(heroRaw, sizeof(heroRaw), t.totalReturnPct, 1);
        fmtSevenSeg(hero, sizeof(hero), heroRaw);
        lcd.setFont(&fonts::Font7);
        lcd.setTextColor(t.anyStale ? C_DATE : tone, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(hero, cx, y + 50);

        char tot[40], dayPct[16];
        fmtPct(dayPct, sizeof(dayPct), t.dayChangePct, 2);
        snprintf(tot, sizeof(tot), "%s %s", dayWhen, dayPct);
        lcd.setFont(&fonts::Font4);
        lcd.setTextColor(dayTone, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(tot, cx, y + 90);
    }

    // A window return, only when the recorded history actually reaches back
    // that far - historyValueDaysAgo() declines rather than stretch a gap into
    // an answer, so each figure stays empty until there is a real week, month
    // or year of daily snapshots behind it. Also declines if the cost basis on
    // file that day differs from today's: a changed quantity or a new position
    // moved the cost basis, and a value comparison across that gap is a
    // deposit or a withdrawal wearing a market return's clothes, not the
    // return itself.
    char p7[24] = "", p30[24] = "", pYtd[24] = "";
    {
        struct tm tNow;
        if (localNow(&tNow)) {
            double v7 = 0, c7 = 0, v30 = 0, c30 = 0, vYtd = 0, cYtd = 0;
            // A cent or two of drift is float rounding across the value's trip
            // through a float in NVS and back; a real deposit or edit moves the
            // cost basis by whole units of currency, not fractions of one.
            const double costTol = 1.0;
            if (historyValueDaysAgo(tNow, 7, &v7, &c7) && v7 > 0 &&
                (c7 - t.cost < costTol && t.cost - c7 < costTol)) {
                char b[16];
                fmtPct(b, sizeof(b), (t.value - v7) / v7 * 100.0, 1);
                snprintf(p7, sizeof(p7), "7d %s", b);
            }
            if (historyValueDaysAgo(tNow, 30, &v30, &c30) && v30 > 0 &&
                (c30 - t.cost < costTol && t.cost - c30 < costTol)) {
                char b[16];
                fmtPct(b, sizeof(b), (t.value - v30) / v30 * 100.0, 1);
                snprintf(p30, sizeof(p30), "30d %s", b);
            }
            // historyYearStart() is the persisted January 1st anchor (see
            // history.h) and covers the whole year once this device has run
            // across at least one New Year's. Until then - or if that anchor
            // was lost to a historyClear() - fall back to the ~4-month ring
            // buffer: "days back" to January 1st is just today's day-of-year,
            // and a tight 14-day gap tolerance (not the 7/30 default of
            // matching the window itself) keeps a late-year query from
            // accepting a point months stale and calling it "January 1st".
            // That fallback only ever resolves January through roughly April,
            // since HISTORY_MAX (~4 months, see history.h) cannot reach
            // further back than that - silent, correctly, the rest of the
            // year until the anchor takes over for good.
            bool haveYtd = historyYearStart(tNow, &vYtd, &cYtd);
            if (!haveYtd && tNow.tm_yday > 0)
                haveYtd = historyValueDaysAgo(tNow, tNow.tm_yday, &vYtd, &cYtd, 14);
            if (haveYtd && vYtd > 0 &&
                (cYtd - t.cost < costTol && t.cost - cYtd < costTol)) {
                char b[16];
                fmtPct(b, sizeof(b), (t.value - vYtd) / vYtd * 100.0, 1);
                snprintf(pYtd, sizeof(pYtd), "YTD %s", b);
            }
        }
    }

    // One status line, in priority order: a wrong total matters more than a
    // stale one, and a stale one matters more than the hint.
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(lgfx::middle_center);
    // Priority: a wrong total beats a stale one, a stale one beats advice, and
    // advice beats a mode hint. Drift is deliberately not gated on the amount
    // mode - it is a percentage, and gating it hid the feature completely.
    int suspect = -1;
    for (int i = 0; i < positionCount && suspect < 0; i++)
        if (positionIsHeld(positions[i]) && positionSuspectMove(positions[i])) suspect = i;

    const int wi = portfolioHasTargets() ? worstDriftIndex(t) : -1;
    if (suspect >= 0) {
        // Ahead of everything else: a figure that is probably wrong matters
        // more than one that is merely off target.
        char msg[72];
        snprintf(msg, sizeof(msg), "%s moved oddly - check for a split",
                 positions[suspect].label);
        lcd.setTextColor(C_DOWN, C_BG);
        lcd.drawString(msg, cx, y + h - 10);
    } else if (t.anyUnconverted) {
        lcd.setTextColor(C_DOWN, C_BG);
        lcd.drawString("some positions excluded - no FX rate", cx, y + h - 10);
    } else if (t.anyStale) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("some prices stale", cx, y + h - 10);
    } else if (wi >= 0) {
        const double d   = positionDriftPct(positions[wi], t);
        const double mag = d < 0 ? -d : d;
        char msg[64];
        char driftBuf[16];
        fmtPct(driftBuf, sizeof(driftBuf), d, 1);
        stripPercent(driftBuf);
        snprintf(msg, sizeof(msg), "%s %s from target", positions[wi].label, driftBuf);
        lcd.setTextColor(mag >= DRIFT_WARN_PCT ? 0xFFAA00 : C_MUTED, C_BG);
        lcd.drawString(msg, cx, y + h - 10);
    } else if (p7[0] || p30[0] || pYtd[0]) {
        lcd.setTextColor(C_LABEL, C_BG);
        const char* parts[3];
        int nParts = 0;
        if (p7[0])   parts[nParts++] = p7;
        if (p30[0])  parts[nParts++] = p30;
        if (pYtd[0]) parts[nParts++] = pYtd;

        // Drops the least essential figure first - YTD, then 30d - if the full
        // line would run past the panel at this font. Measured with the font
        // this line actually draws in (Font2 was just set above), rather than
        // guessed from a character budget a different font size would
        // invalidate.
        char win[88];
        for (int keep = nParts; keep >= 1; keep--) {
            win[0] = '\0';
            for (int i = 0; i < keep; i++) {
                if (i > 0) strncat(win, "   ", sizeof(win) - strlen(win) - 1);
                strncat(win, parts[i], sizeof(win) - strlen(win) - 1);
            }
            if (lcd.textWidth(win) <= W - 12) break;
        }
        lcd.drawString(win, cx, y + h - 10);
    } else {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString(settings.amountMode == 0 ? "amounts hidden"
                       : settings.amountMode == 1 && reveal ? "hiding again shortly"
                       : settings.amountMode == 1 ? "tap USER for values"
                       : "",
                       cx, y + h - 10);
    }
}

// ── Allocation ────────────────────────────────────────────────────────────────
void drawAllocation() {
    const int y  = ROW1_Y;
    const int h  = DIV3_Y - y;
    const int cx = W / 2;

    lcd.fillRect(0, y, W, h, C_BG);
    lcd.fillRect(0, y, W, 3, C_ACCENT);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_ACCENT, C_BG);
    lcd.setTextDatum(lgfx::top_left);
    lcd.drawString("ALLOCATION", 6, y + 6);

    const PortfolioTotals t = portfolioTotals();

    // Collect the targeted sleeve in the order the editor lists it, so the rows
    // match what you typed rather than a ranking that moves under you. Only the
    // first GRID_SLOTS are drawn as rows - the screen only has room for six -
    // but that cap must never reach the worst-drift figure below: this screen
    // and the portfolio overview (worstDriftIndex(), unbounded) have to agree
    // on which fund is worst, or a 7th+ targeted fund could be silently absent
    // from both its own row and the one figure meant to catch a bad drift.
    int idx[GRID_SLOTS];
    int n = 0, totalTargeted = 0;
    for (int i = 0; i < positionCount; i++) {
        if (!positionIsHeld(positions[i]) || positions[i].target <= 0 || !positions[i].ok) continue;
        totalTargeted++;
        if (n < GRID_SLOTS) idx[n++] = i;
    }

    char meta[40];
    if (n == 0)                    snprintf(meta, sizeof(meta), "no targets");
    else if (totalTargeted > n)    snprintf(meta, sizeof(meta), "%d of %d funds", n, totalTargeted);
    else                           snprintf(meta, sizeof(meta), "%d funds", totalTargeted);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::top_right);
    lcd.drawString(meta, W - 6, y + 6);

    if (!t.valid || n == 0) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(t.valid ? "add a target % to a position" : "waiting for prices",
                       cx, y + h / 2 - 8);
        if (t.valid)
            lcd.drawString("SYMBOL,QTY,COST,LABEL,TARGET", cx, y + h / 2 + 10);
        return;
    }

    // One row per fund. The bar is drawn against the largest target so the rows
    // share a scale and can be compared by eye; the notch is where the target
    // sits, which is what makes an over- or under-weight visible at a glance
    // rather than something you work out from two numbers.
    double scale = 0;
    for (int k = 0; k < n; k++) {
        const double a = positionSleevePct(positions[idx[k]], t);
        const double g = positionTargetPct(positions[idx[k]], t);
        if (a > scale) scale = a;
        if (g > scale) scale = g;
    }
    if (scale < 1.0) scale = 1.0;

    const int rowTop = y + 24;
    const int rowH   = (h - 24 - 18) / (n > 0 ? n : 1);

    // A label may be up to LABEL_MAX characters, which at this font can be
    // twice the width of a fixed column - so the bar starts clear of the
    // widest one actually on screen rather than at a guessed offset.
    lcd.setFont(&fonts::Font2);
    int labelW = 0;
    for (int k = 0; k < n; k++) {
        const int lw = lcd.textWidth(positions[idx[k]].label);
        if (lw > labelW) labelW = lw;
    }
    int barX = 12 + labelW;
    if (barX < 70)  barX = 70;
    if (barX > 132) barX = 132;      // always leave the bar something to say
    const int barW = 220 - barX;

    // The worst-drift figure below has to come from the same, unbounded scan
    // worstDriftIndex() already does for the portfolio overview - not from just
    // the rows drawn here - or a 7th+ targeted fund could be the one that most
    // needs rebalancing and never appear in either place.
    const int worst = worstDriftIndex(t);
    double worstMag = 0.0;
    if (worst >= 0) {
        const double d = positionDriftPct(positions[worst], t);
        worstMag = d < 0 ? -d : d;
    }

    for (int k = 0; k < n; k++) {
        const Position& p = positions[idx[k]];
        const double actual = positionSleevePct(p, t);
        const double target = positionTargetPct(p, t);
        const double drift  = actual - target;
        const double mag    = drift < 0 ? -drift : drift;

        const int ry = rowTop + k * rowH + rowH / 2;
        const uint32_t tone = (mag >= DRIFT_WARN_PCT) ? 0xFFAA00 : C_ACCENT;

        lcd.setFont(&fonts::Font2);
        lcd.setTextDatum(lgfx::middle_left);
        lcd.setTextColor(C_DATE, C_BG);
        lcd.drawString(p.label, 6, ry);

        const int barH = (rowH > 14) ? 8 : 6;
        lcd.fillRect(barX, ry - barH / 2, barW, barH, C_PANEL);
        int fill = (int)((actual / scale) * barW);
        if (fill < 0) fill = 0;
        if (fill > barW) fill = barW;
        lcd.fillRect(barX, ry - barH / 2, fill, barH, tone);

        int tick = (int)((target / scale) * barW);
        if (tick < 0) tick = 0;
        if (tick > barW - 1) tick = barW - 1;
        lcd.fillRect(barX + tick, ry - barH / 2 - 3, 1, barH + 6, C_PRICE);

        char pct[16];
        fmtPct(pct, sizeof(pct), actual, 1);
        lcd.setTextDatum(lgfx::middle_right);
        lcd.setTextColor(C_PRICE, C_BG);
        lcd.drawString(pct + 1, barX + barW + 34, ry);      // drop the leading '+'

        char dr[16];
        fmtPct(dr, sizeof(dr), drift, 1);
        stripPercent(dr);                                    // points, not percent
        lcd.setTextColor(mag >= DRIFT_WARN_PCT ? 0xFFAA00 : C_LABEL, C_BG);
        lcd.drawString(dr, W - 6, ry);
    }

    // The action the rows imply, in money when amounts are allowed.
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(lgfx::middle_center);
    if (worst < 0 || worstMag < 0.5) {
        lcd.setTextColor(C_UP, C_BG);
        lcd.drawString("on target", cx, y + h - 8);
    } else {
        const Position& p = positions[worst];
        const double amt = positionRebalanceAmount(p, t);
        char msg[72], dr[16];
        fmtPct(dr, sizeof(dr), worstMag, 1);
        stripPercent(dr);
        if (portfolioRevealActive()) {
            char money[28];
            fmtMoney(money, sizeof(money), amt < 0 ? -amt : amt);
            snprintf(msg, sizeof(msg), "%s %s by %s %s", p.label,
                     amt >= 0 ? "light" : "heavy", money, settings.baseCurrency);
        } else {
            snprintf(msg, sizeof(msg), "%s %s by %s points", p.label,
                     amt >= 0 ? "light" : "heavy", dr + 1);
        }
        lcd.setTextColor(worstMag >= 2.0 ? 0xFFAA00 : C_MUTED, C_BG);
        lcd.drawString(msg, cx, y + h - 8);
    }
}

// ── Loan ──────────────────────────────────────────────────────────────────────
void drawLoan() {
    const int y  = ROW1_Y;
    const int h  = DIV3_Y - y;
    const int cx = W / 2;

    lcd.fillRect(0, y, W, h, C_BG);
    lcd.fillRect(0, y, W, 3, C_ACCENT);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_ACCENT, C_BG);
    lcd.setTextDatum(lgfx::top_left);
    lcd.drawString("LOAN", 6, y + 6);

    const LoanState L = loanCompute();

    char meta[32];
    if (!L.configured)          snprintf(meta, sizeof(meta), "not set up");
    else if (L.drawn == 0)      snprintf(meta, sizeof(meta), "starts in %dd", L.daysToNext);
    else                        snprintf(meta, sizeof(meta), "%d/%d drawn", L.drawn, L.total);
    lcd.setTextColor(C_LABEL, C_BG);
    lcd.setTextDatum(lgfx::top_right);
    lcd.drawString(meta, W - 6, y + 6);

    lcd.setTextDatum(lgfx::middle_center);
    if (!L.configured) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("set the first drawdown date", cx, y + h / 2 - 10);
        lcd.drawString("in Settings to use this view", cx, y + h / 2 + 10);
        return;
    }
    if (!L.valid) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("nothing drawn yet", cx, y + h / 2);
        return;
    }

    // Net equity: what is left after repaying today. Portfolio value on its own
    // flatters a borrowed position by exactly the amount owed, and that gap
    // widens every month the interest capitalises.
    const bool reveal = portfolioRevealActive();
    const uint32_t tone = (L.equity >= 0) ? C_UP : C_DOWN;

    char heroRaw[32], hero[32];
    if (reveal) fmtMoney(heroRaw, sizeof(heroRaw), L.equity);
    else        fmtPct(heroRaw, sizeof(heroRaw),
                       (L.owed > 0) ? (L.equity / L.owed) * 100.0 : 0.0, 1);
    fmtSevenSeg(hero, sizeof(hero), heroRaw);
    lcd.setFont(&fonts::Font7);
    lcd.setTextColor(L.anyUnpriced ? C_DATE : tone, C_BG);
    lcd.drawString(hero, cx, y + 48);

    char line[112];
    if (reveal) {
        char pr[28], in[28], va[28];
        fmtMoney(pr, sizeof(pr), L.principal);
        fmtMoney(in, sizeof(in), L.interest);
        fmtMoney(va, sizeof(va), L.value);
        snprintf(line, sizeof(line), "%s drawn + %s int -> %s", pr, in, va);
    } else if (L.daysToNext >= 0) {
        snprintf(line, sizeof(line), "next tranche in %d days", L.daysToNext);
    } else {
        snprintf(line, sizeof(line), "fully drawn");
    }
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString(line, cx, y + 90);

    // The spread is what says whether the borrowing is doing its job, and it is
    // the number that turns negative first.
    lcd.setTextDatum(lgfx::middle_center);
    if (L.anyUnconverted) {
        lcd.setTextColor(C_DOWN, C_BG);
        lcd.drawString("some positions excluded - no FX rate", cx, y + h - 10);
    } else if (L.anyUnpriced) {
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.drawString("waiting for prices", cx, y + h - 10);
    } else {
        char ret[16], cost[16], spread[48];
        fmtPct(ret,  sizeof(ret),  L.returnPct, 1);
        fmtPct(cost, sizeof(cost), L.ratePct,   1);
        stripPercent(cost);
        snprintf(spread, sizeof(spread), "%s return vs %s%% cost", ret, cost + 1);
        lcd.setTextColor(L.returnPct >= L.ratePct ? C_UP : C_DOWN, C_BG);
        lcd.drawString(spread, cx, y + h - 10);
    }
}

// ── Footer ────────────────────────────────────────────────────────────────────
static uint32_t phaseColor(MarketPhase p, bool active) {
    switch (p) {
        case PHASE_PRE:  return active ? 0xFFAA00 : 0x2E1A00;
        case PHASE_OPEN: return active ? 0x00CC44 : 0x002E10;
        case PHASE_POST: return active ? 0x3399CC : 0x101E33;
        default:         return 0x0E1520;
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

// MarketPhase's declaration order (CLOSED, PRE, OPEN, POST) is a data-model
// convenience, not a business ranking - comparing it directly picked POST over
// OPEN. Between 17:30 and 20:00 CET that named a Xetra position's after-hours
// session over a still-open regional one as "most active".
static int phaseActivity(MarketPhase p) {
    switch (p) {
        case PHASE_OPEN: return 3;
        case PHASE_PRE:  return 2;
        case PHASE_POST: return 1;
        default:         return 0;   // PHASE_CLOSED
    }
}

// The bar describes the instrument on screen; in the grid it describes the one
// whose session is most active, so it is never claiming "closed" while
// something on the page is trading.
static const TradingSession& displayedSession() {
    if (s_view == VIEW_DETAIL && positionCount > 0 && s_detail < positionCount
        && positions[s_detail].session)
        return *positions[s_detail].session;

    struct tm t;
    if (positionCount > 0 && localNow(&t)) {
        const TradingSession* best = positions[0].session ? positions[0].session : &SESSION_EQUITY;
        MarketPhase bestPhase = phaseAtMinute(*best, t.tm_wday, t.tm_hour * 60 + t.tm_min);
        for (int i = 1; i < positionCount; i++) {
            const TradingSession* s = positions[i].session ? positions[i].session : &SESSION_EQUITY;
            const MarketPhase ph = phaseAtMinute(*s, t.tm_wday, t.tm_hour * 60 + t.tm_min);
            if (phaseActivity(ph) > phaseActivity(bestPhase)) { bestPhase = ph; best = s; }
        }
        return *best;
    }
    return SESSION_EQUITY;
}

void drawFooter() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    struct tm t;
    if (!localNow(&t)) {
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

    const int by = PRG_Y, bh = 8;
    auto xAt = [](int m) -> int { return (int)((long)m * W / MKT_DAY_MINS); };

    int runStart = 0;
    MarketPhase runPhase = phaseAtMinute(sess, wday, 0);
    bool allClosed = true;

    for (int m = 1; m <= MKT_DAY_MINS; m++) {
        const MarketPhase p = (m < MKT_DAY_MINS) ? phaseAtMinute(sess, wday, m) : PHASE_CLOSED;
        if (m == MKT_DAY_MINS || p != runPhase) {
            const int x1 = xAt(runStart);
            const int x2 = (m == MKT_DAY_MINS) ? W : xAt(m);
            const bool active = (curMin >= runStart && curMin < m);
            if (x2 > x1) lcd.fillRect(x1, by, x2 - x1, bh, phaseColor(runPhase, active));
            if (runPhase != PHASE_CLOSED) allClosed = false;
            if (runStart > 0) lcd.drawFastVLine(x1, by, bh, 0x2A4A6A);
            runStart = m;
            runPhase = p;
        }
    }

    const int px = xAt(curMin);
    if (px > 1 && px < W - 2) {
        lcd.drawFastVLine(px - 1, by, bh, 0x224433);
        lcd.drawFastVLine(px,     by, bh, 0xFFFFFF);
        lcd.drawFastVLine(px + 1, by, bh, 0x224433);
    }

    int minsLeft = 0;
    {
        int w = wday, m = curMin;
        const int limit = 8 * MKT_DAY_MINS;
        while (minsLeft < limit) {
            if (++m >= MKT_DAY_MINS) { m = 0; w = (w + 1) % 7; }
            minsLeft++;
            if (phaseAtMinute(sess, w, m) != curPhase) break;
        }
    }

    char label[44];
    uint32_t labelColor;
    switch (curPhase) {
        case PHASE_PRE:  labelColor = 0xFFAA00; break;
        case PHASE_OPEN: labelColor = C_UP;     break;
        case PHASE_POST: labelColor = 0x3399CC; break;
        default:         labelColor = C_MUTED;  break;
    }

    if (allClosed)          snprintf(label, sizeof(label), "%s  %s  weekend", sess.label, phaseName(curPhase));
    else if (minsLeft >= 60) snprintf(label, sizeof(label), "%s  %s  %dh %02dm left",
                                      sess.label, phaseName(curPhase), minsLeft / 60, minsLeft % 60);
    else                     snprintf(label, sizeof(label), "%s  %s  %dm left",
                                      sess.label, phaseName(curPhase), minsLeft);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(labelColor, C_BG);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(label, W / 2, PRG_Y + 9);

    // Page indicator matters more than the countdown when there is paging.
    if (s_view == VIEW_POSITIONS && displayPageCount() > 1) {
        char pg[24];
        snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, displayPageCount());
        lcd.setTextColor(C_LABEL, C_BG);
        lcd.setTextDatum(lgfx::top_right);
        lcd.drawString(pg, W - 4, PRG_Y + 9);
    } else if (s_refreshCountdown >= 0) {
        char cbuf[16];
        snprintf(cbuf, sizeof(cbuf), "~%ds", s_refreshCountdown);
        lcd.setTextColor(C_MUTED, C_BG);
        lcd.setTextDatum(lgfx::top_right);
        lcd.drawString(cbuf, W - 4, PRG_Y + 9);
    }
}

void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    if (s_view == VIEW_POSITIONS && positionCount > 0) {
        lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
        lcd.drawFastVLine(107, ROW1_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(212, ROW1_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(107, ROW2_Y, ROW_H, C_DIV);
        lcd.drawFastVLine(212, ROW2_Y, ROW_H, C_DIV);
    }
}
