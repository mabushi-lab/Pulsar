// Plusar — Market Dashboard
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ── Display (T-Display S3 canonical 8-bit parallel config) ───────────────────
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

// ── Pins ──────────────────────────────────────────────────────────────────────
static const int PIN_POWER = 15;
static const int BTN_BOOT  = 0;
static const int BTN_USER  = 14;

// ── Layout (landscape 320×170) ────────────────────────────────────────────────
// Header (18px) | div | Row1 (64px) | div | Row2 (64px) | div | Progress (21px)
static const int W       = 320;
static const int H       = 170;
static const int HDR_H   = 18;
static const int DIV1_Y  = 18;
static const int ROW1_Y  = 19;
static const int ROW_H   = 64;
static const int DIV2_Y  = 83;
static const int ROW2_Y  = 84;
static const int DIV3_Y  = 148;
static const int PRG_Y   = 149;
static const int PRG_H   = 21;

// Three columns: x=107 and x=212 are the 1-px vertical dividers
static const int COL_X[3] = { 0,   108, 213 };
static const int COL_W[3] = { 107, 104, 107 };

// ── Colours ───────────────────────────────────────────────────────────────────
static const uint32_t C_BG       = 0x06080F;
static const uint32_t C_HDR      = 0x0B1420;
static const uint32_t C_PANEL    = 0x080C18;
static const uint32_t C_DIV      = 0x1E3A52;
static const uint32_t C_PRICE    = 0xEEF2FF;
static const uint32_t C_DATE     = 0x7A93AC;
static const uint32_t C_LABEL    = 0x3D5A73;
static const uint32_t C_UP       = 0x00CC55;
static const uint32_t C_DOWN     = 0xFF3344;
static const uint32_t C_MUTED    = 0x2A3F52;
static const uint32_t C_WEATHER  = 0xFFAA33;
static const uint32_t C_WIFI_OK  = 0x00CC44;
static const uint32_t C_WIFI_ERR = 0xFF3333;

// ── Market data ───────────────────────────────────────────────────────────────
struct MarketItem {
    const char* label;
    const char* stooqSym;   // raw symbol — ^ will be encoded in URL
    uint32_t    accentColor;
    float       price;
    float       openPrice;
    float       changePct;
    bool        fetched;    // true after first attempt (ok or fail)
    bool        ok;
};

static MarketItem markets[6] = {
    { "S&P 500",  "^spx",    0x33CCFF, 0, 0, 0, false, false },
    { "STOXX 50", "^sx5e",   0x33CCFF, 0, 0, 0, false, false },
    { "Emrg Mkt", "eem.us",  0x33CCFF, 0, 0, 0, false, false },
    { "Gold",     "xauusd",  0xFFAA33, 0, 0, 0, false, false },
    { "Silver",   "xagusd",  0xCCDDEE, 0, 0, 0, false, false },
    { "Bitcoin",  "btcusd",  0xF7931A, 0, 0, 0, false, false },
};

// ── Weather state ─────────────────────────────────────────────────────────────
static float tempC    = 0.0f;
static int   wxCode   = -1;
static bool  wxFetched = false;

// ── NTP / server ──────────────────────────────────────────────────────────────
static WiFiUDP   ntpUdp;
static NTPClient ntp(ntpUdp, "pool.ntp.org", UTC_OFFSET_SEC);
static WebServer server(80);
static bool      fullBright = true;

// ── Helpers ───────────────────────────────────────────────────────────────────
static const char* wmoDesc(int code) {
    if (code == 0)  return "Clear";
    if (code <= 2)  return "Mainly Clear";
    if (code == 3)  return "Overcast";
    if (code <= 48) return "Foggy";
    if (code <= 55) return "Drizzle";
    if (code <= 65) return "Rain";
    if (code <= 75) return "Snow";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow Showers";
    if (code == 95) return "Thunderstorm";
    if (code <= 99) return "Heavy Storm";
    return "Unknown";
}

// Format a market price for Font4 display
static void fmtPrice(char* buf, size_t n, float price) {
    if (price >= 10000) {
        long p = lroundf(price);
        snprintf(buf, n, "%ld,%03ld", p / 1000, p % 1000);
    } else if (price >= 1000) {
        long p = lroundf(price);
        snprintf(buf, n, "%ld,%03ld", p / 1000, p % 1000);
    } else if (price >= 100) {
        snprintf(buf, n, "%.1f", price);
    } else {
        snprintf(buf, n, "%.2f", price);
    }
}

// Format open price as short "o:XXXX" label for Font2
static void fmtOpen(char* buf, size_t n, float price) {
    if (price >= 1000)
        snprintf(buf, n, "o:%ld", lroundf(price));
    else if (price >= 100)
        snprintf(buf, n, "o:%.1f", price);
    else
        snprintf(buf, n, "o:%.2f", price);
}

// Extract the Nth comma-separated field from a CSV line
static float csvFloat(const String& line, int fieldIdx) {
    int pos = 0;
    for (int i = 0; i < fieldIdx; i++) {
        pos = line.indexOf(',', pos);
        if (pos < 0) return 0;
        pos++;
    }
    int end = line.indexOf(',', pos);
    if (end < 0) end = line.length();
    return line.substring(pos, end).toFloat();
}

static String csvStr(const String& line, int fieldIdx) {
    int pos = 0;
    for (int i = 0; i < fieldIdx; i++) {
        pos = line.indexOf(',', pos);
        if (pos < 0) return "";
        pos++;
    }
    int end = line.indexOf(',', pos);
    if (end < 0) end = line.length();
    return line.substring(pos, end);
}

// ── Drawing ───────────────────────────────────────────────────────────────────
static void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    lcd.drawFastVLine(107, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW1_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(107, ROW2_Y, ROW_H, C_DIV);
    lcd.drawFastVLine(212, ROW2_Y, ROW_H, C_DIV);
}

static void drawHeader() {
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

static void drawMarketPanel(int idx) {
    const MarketItem& m = markets[idx];
    int col = idx % 3;
    int x   = COL_X[col];
    int w   = COL_W[col];
    int y   = (idx < 3) ? ROW1_Y : ROW2_Y;
    int cx  = x + w / 2;

    lcd.fillRect(x, y, w, ROW_H, C_PANEL);

    // Label — accent colour
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

    // Price — Font4, near-white
    char priceBuf[12];
    fmtPrice(priceBuf, sizeof(priceBuf), m.price);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_PRICE, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, y + 14);

    // Change % — green or red
    char changeBuf[10];
    snprintf(changeBuf, sizeof(changeBuf), "%+.2f%%", m.changePct);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(m.changePct >= 0 ? C_UP : C_DOWN, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(changeBuf, cx, y + 33);

    // Open reference
    char openBuf[12];
    fmtOpen(openBuf, sizeof(openBuf), m.openPrice);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.drawString(openBuf, cx, y + 47);
}

static void drawAllMarkets() {
    for (int i = 0; i < 6; i++) drawMarketPanel(i);
}

static void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);
    int secs = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    int pct  = (int)((long)secs * 100 / 86400);

    const int PAD = 4;
    const int LW  = 26;
    const int PW  = 34;
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

    int fw = (bw * pct) / 100;
    uint32_t fc = (pct < 50) ? C_WIFI_OK : (pct < 80) ? 0xFFAA00 : 0xFF4400;
    if (fw > 3) lcd.fillRoundRect(bx, by, fw, bh, 3, fc);

    char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.setTextDatum(lgfx::middle_left);
    lcd.drawString(buf, bx + bw + PAD, by + bh / 2);
}

static void drawAll() {
    lcd.fillScreen(C_BG);
    drawHeader();
    drawAllMarkets();
    drawProgress();
    drawDividers();
}

// ── Fetching ──────────────────────────────────────────────────────────────────
static void fetchWeather() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code",
        (float)LATITUDE, (float)LONGITUDE);

    HTTPClient http;
    http.setTimeout(6000);
    http.begin(url);
    if (http.GET() == HTTP_CODE_OK) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            tempC  = doc["current"]["temperature_2m"].as<float>();
            wxCode = doc["current"]["weather_code"].as<int>();
        }
    }
    http.end();
    wxFetched = true;
}

// Stooq CSV API — single HTTPS request for all 6 symbols.
// Response: Symbol,Date,Time,Open,High,Low,Close,Volume  (fields 0,3,6)
// Change % is intraday (close vs open). Stooq returns the most recent session.
static void fetchMarkets() {
    // ^ must be %-encoded in the query string
    const char* url =
        "https://stooq.com/q/l/"
        "?s=%5Espx,%5Esx5e,eem.us,xauusd,xagusd,btcusd"
        "&f=sd2t2ohlcv&h&e=csv";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(client, url);
    http.addHeader("User-Agent", "Mozilla/5.0");

    bool ok = (http.GET() == HTTP_CODE_OK);
    String csv = ok ? http.getString() : "";
    http.end();

    for (int i = 0; i < 6; i++) markets[i].fetched = true;

    if (!ok || csv.length() < 10) {
        for (int i = 0; i < 6; i++) markets[i].ok = false;
        return;
    }

    // Skip header row
    int lineStart = csv.indexOf('\n');
    if (lineStart < 0) return;
    lineStart++;

    while (lineStart < (int)csv.length()) {
        int lineEnd = csv.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = csv.length();
        String line = csv.substring(lineStart, lineEnd);
        line.trim();

        if (line.length() > 10) {
            String sym = csvStr(line, 0);
            sym.replace("^", "");
            sym.toUpperCase();

            float openVal  = csvFloat(line, 3);
            float closeVal = csvFloat(line, 6);

            for (int i = 0; i < 6; i++) {
                String mSym = String(markets[i].stooqSym);
                mSym.replace("^", "");
                mSym.toUpperCase();

                if (sym == mSym && closeVal > 0) {
                    markets[i].price     = closeVal;
                    markets[i].openPrice = openVal;
                    markets[i].changePct = (openVal > 0)
                        ? (closeVal - openVal) / openVal * 100.0f : 0;
                    markets[i].ok = true;
                    break;
                }
            }
        }
        lineStart = lineEnd + 1;
    }
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
static void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 40) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
        ntp.begin();
        ntp.forceUpdate();
        fetchWeather();
        fetchMarkets();
    }
}

// ── HTTP handlers (info page + manual refresh trigger) ───────────────────────
static void onRoot() {
    char body[512];
    snprintf(body, sizeof(body), R"HTML(<!DOCTYPE html>
<html><head><title>Plusar</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#06080F;color:#EEF2FF;font-family:monospace;max-width:420px;margin:40px auto;padding:20px}
h2{color:#33CCFF}p{color:#7A93AC;font-size:13px}
button{padding:10px 20px;background:#0D1B2A;border:1px solid #33CCFF;color:#33CCFF;border-radius:4px;cursor:pointer;font-size:14px}
button:hover{filter:brightness(1.3)}</style></head>
<body><h2>Plusar Market Dashboard</h2>
<p>IP: %s</p>
<button onclick="fetch('/refresh',{method:'POST'})">Refresh Markets Now</button>
</body></html>)HTML", WiFi.localIP().toString().c_str());
    server.send(200, "text/html", body);
}

static bool refreshRequested = false;
static void onRefresh() {
    refreshRequested = true;
    server.send(200, "application/json", R"({"ok":true})");
}

// ── Buttons ───────────────────────────────────────────────────────────────────
static void checkButtons() {
    static bool     prevBoot = HIGH, prevUser = HIGH;
    static uint32_t lastMs   = 0;
    if (millis() - lastMs < 50) return;
    lastMs = millis();

    bool boot = digitalRead(BTN_BOOT);
    bool user = digitalRead(BTN_USER);

    if (prevBoot == HIGH && boot == LOW) {
        fullBright = !fullBright;
        lcd.setBrightness(fullBright ? 255 : 60);
    }
    if (prevUser == HIGH && user == LOW) {
        // Force an immediate market refresh
        refreshRequested = true;
    }
    prevBoot = boot;
    prevUser = user;
}

// ── Entry points ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);
    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_USER, INPUT_PULLUP);

    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(255);

    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString("Connecting...", W / 2, H / 2);

    connectWiFi();

    server.on("/",        HTTP_GET,  onRoot);
    server.on("/refresh", HTTP_POST, onRefresh);
    server.begin();

    drawAll();
}

void loop() {
    checkButtons();
    server.handleClient();

    static uint32_t lastSec     = 0;
    static uint32_t lastWeather = 0;
    static uint32_t lastMarkets = 0;
    static int      lastPct     = -1;
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    uint32_t now    = millis();
    bool     wifiOk = (WiFi.status() == WL_CONNECTED);

    if (!wifiOk) {
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
            lastRetry = now;
            connectWiFi();
            if (WiFi.status() == WL_CONNECTED) { drawAll(); wifiWas = true; }
            else drawHeader();
        }
        return;
    }
    if (!wifiWas) { wifiWas = true; drawAll(); }

    // Manual refresh via button or /refresh endpoint
    if (refreshRequested) {
        refreshRequested = false;
        lastMarkets = now;
        fetchMarkets();
        drawAllMarkets();
        drawDividers();
    }

    // 1-second tick
    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();

        // Header: full redraw once per minute (date string changes)
        if (ntp.getSeconds() == 0) {
            drawHeader();
            drawDividers();
        }

        // Progress bar — only when % changes (~once per 14 min)
        time_t     epoch = ntp.getEpochTime();
        struct tm *t     = localtime(&epoch);
        int secs = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
        int pct  = (int)((long)secs * 100 / 86400);
        if (pct != lastPct) { lastPct = pct; drawProgress(); }
    }

    // Weather — every 10 minutes
    if (lastWeather == 0 || now - lastWeather >= 600000UL) {
        lastWeather = now;
        fetchWeather();
        drawHeader();
        drawDividers();
    }

    // Markets — every 5 minutes
    if (lastMarkets == 0 || now - lastMarkets >= 300000UL) {
        lastMarkets = now;
        fetchMarkets();
        drawAllMarkets();
        drawDividers();
    }
}
