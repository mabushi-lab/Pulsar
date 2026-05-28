// Plusar — Desk Dashboard
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
            cfg.offset_rotation = 1;  // landscape by default
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

static LGFX        lcd;
static LGFX_Sprite clockSprite(&lcd);

// ── Pins ──────────────────────────────────────────────────────────────────────
static const int PIN_POWER = 15;
static const int BTN_BOOT  = 0;
static const int BTN_USER  = 14;

// ── Layout (landscape 320×170) ────────────────────────────────────────────────
// Row boundaries
static const int W       = 320;
static const int H       = 170;
static const int HDR_H   = 20;   // header bar
static const int DIV1_Y  = 20;
static const int CLK_Y   = 21;   // clock sprite top
static const int CLK_H   = 57;   // Font7=48px + 9px breathing room
static const int DIV2_Y  = 78;
static const int PNL_Y   = 79;   // three-panel row top
static const int PNL_H   = 65;
static const int DIV3_Y  = 144;
static const int PRG_Y   = 145;  // day-progress bar
static const int PRG_H   = 25;

// Panel columns — x=107 and x=212 are the 1-px vertical dividers
static const int PA_X = 0,   PA_W = 107;  // weather
static const int PB_X = 108, PB_W = 104;  // crypto
static const int PC_X = 213, PC_W = 107;  // status

// ── Colours ───────────────────────────────────────────────────────────────────
static const uint32_t C_BG       = 0x06080F;
static const uint32_t C_HDR      = 0x0B1420;
static const uint32_t C_PANEL    = 0x080C18;
static const uint32_t C_DIV      = 0x1E3A52;
static const uint32_t C_CLOCK    = 0xEEF2FF;
static const uint32_t C_CLK_SEC  = 0x445566;  // dimmed seconds
static const uint32_t C_DATE     = 0x7A93AC;
static const uint32_t C_LABEL    = 0x3D5A73;
static const uint32_t C_WEATHER  = 0xFFAA33;
static const uint32_t C_CRYPTO   = 0xF7931A;
static const uint32_t C_STATUS   = 0x33CCFF;
static const uint32_t C_MUTED    = 0x2A3F52;
static const uint32_t C_WIFI_OK  = 0x00CC44;
static const uint32_t C_WIFI_ERR = 0xFF3333;

// ── State ─────────────────────────────────────────────────────────────────────
static float  tempC     = 0.0f;
static int    wxCode    = -1;
static int    humidity  = -1;
static float  windKmh   = 0.0f;
static bool   wxFetched = false;   // true after first attempt (success or fail)

static long   btcUsd    = -1;
static bool   btcFetched = false;  // same pattern

static String statusMsg = "";
static bool   hasStatus = false;
static bool   fullBright = true;

// ── NTP / server ──────────────────────────────────────────────────────────────
static WiFiUDP   ntpUdp;
static NTPClient ntp(ntpUdp, "pool.ntp.org", UTC_OFFSET_SEC);
static WebServer server(80);

// ── Helpers ───────────────────────────────────────────────────────────────────
static const char* wmoDesc(int code) {
    if (code == 0)  return "Clear Sky";
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

static void fmtPrice(char* buf, size_t n, long price) {
    if (price >= 1000000)
        snprintf(buf, n, "$%ld,%03ld,%03ld",
                 price / 1000000, (price / 1000) % 1000, price % 1000);
    else if (price >= 1000)
        snprintf(buf, n, "$%ld,%03ld", price / 1000, price % 1000);
    else
        snprintf(buf, n, "$%ld", price);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static void drawDividers() {
    lcd.drawFastHLine(0, DIV1_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV2_Y, W, C_DIV);
    lcd.drawFastHLine(0, DIV3_Y, W, C_DIV);
    lcd.drawFastVLine(107, PNL_Y, PNL_H, C_DIV);
    lcd.drawFastVLine(212, PNL_Y, PNL_H, C_DIV);
}

static void drawHeader() {
    lcd.fillRect(0, 0, W, HDR_H, C_HDR);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);
    char       dateBuf[24];
    strftime(dateBuf, sizeof(dateBuf), "%a, %b %d  %Y", t);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_DATE, C_HDR);
    lcd.setTextDatum(lgfx::middle_left);
    lcd.drawString(dateBuf, 5, HDR_H / 2);

    // Weather summary — centre of header
    if (wxCode >= 0) {
        char wx[32];
        snprintf(wx, sizeof(wx), "%.1f%cC  %s", tempC, '\xB0', wmoDesc(wxCode));
        lcd.setTextColor(C_WEATHER, C_HDR);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString(wx, W / 2, HDR_H / 2);
    }

    // WiFi status dot — top-right
    bool ok = (WiFi.status() == WL_CONNECTED);
    lcd.fillCircle(W - 8, HDR_H / 2, 4, ok ? C_WIFI_OK : C_WIFI_ERR);
}

static void drawClock() {
    clockSprite.fillSprite(C_BG);
    clockSprite.setFont(&fonts::Font7);
    clockSprite.setTextSize(1);

    char hm[7], sec[4];
    snprintf(hm,  sizeof(hm),  "%02d:%02d", ntp.getHours(), ntp.getMinutes());
    snprintf(sec, sizeof(sec), ":%02d", ntp.getSeconds());

    // HH:MM bright, :SS dimmed — compute widths for manual positioning
    int hmW  = clockSprite.textWidth(hm);
    int secW = clockSprite.textWidth(sec);
    int startX = (W - hmW - secW) / 2;

    clockSprite.setTextDatum(lgfx::middle_left);
    clockSprite.setTextColor(C_CLOCK);
    clockSprite.drawString(hm, startX, CLK_H / 2);
    clockSprite.setTextColor(C_CLK_SEC);
    clockSprite.drawString(sec, startX + hmW, CLK_H / 2);

    clockSprite.pushSprite(0, CLK_Y);
}

static void drawWeatherPanel() {
    int cx = PA_X + PA_W / 2;
    lcd.fillRect(PA_X, PNL_Y, PA_W, PNL_H, C_PANEL);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString("WEATHER", cx, PNL_Y + 3);

    if (!wxFetched) {
        lcd.setTextColor(C_MUTED, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Fetching...", cx, PNL_Y + PNL_H / 2);
        return;
    }
    if (wxCode < 0) {
        lcd.setTextColor(C_WIFI_ERR, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Offline", cx, PNL_Y + PNL_H / 2);
        return;
    }

    // Temperature — Font4, amber
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f%cC", tempC, '\xB0');
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_WEATHER, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(tempBuf, cx, PNL_Y + 14);

    // Condition string
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DATE, C_PANEL);
    lcd.drawString(wmoDesc(wxCode), cx, PNL_Y + 35);

    // Humidity + wind
    char sub[28];
    if (humidity >= 0)
        snprintf(sub, sizeof(sub), "Hum %d%%  W %.0fkm/h", humidity, windKmh);
    else
        snprintf(sub, sizeof(sub), "Wind %.0f km/h", windKmh);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.drawString(sub, cx, PNL_Y + 48);
}

static void drawCryptoPanel() {
    int cx = PB_X + PB_W / 2;
    lcd.fillRect(PB_X, PNL_Y, PB_W, PNL_H, C_PANEL);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString("BTC / USD", cx, PNL_Y + 3);

    if (!btcFetched) {
        lcd.setTextColor(C_MUTED, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Fetching...", cx, PNL_Y + PNL_H / 2);
        return;
    }
    if (btcUsd < 0) {
        lcd.setTextColor(C_WIFI_ERR, C_PANEL);
        lcd.setTextDatum(lgfx::middle_center);
        lcd.drawString("Offline", cx, PNL_Y + PNL_H / 2);
        return;
    }

    char priceBuf[16];
    fmtPrice(priceBuf, sizeof(priceBuf), btcUsd);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_CRYPTO, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString(priceBuf, cx, PNL_Y + 14);

    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DATE, C_PANEL);
    lcd.drawString("Bitcoin", cx, PNL_Y + 35);

    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.drawString("5-min refresh", cx, PNL_Y + 48);
}

static void drawStatusPanel() {
    int cx = PC_X + PC_W / 2;
    lcd.fillRect(PC_X, PNL_Y, PC_W, PNL_H, C_PANEL);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_LABEL, C_PANEL);
    lcd.setTextDatum(lgfx::top_center);
    lcd.drawString("STATUS", cx, PNL_Y + 3);

    if (hasStatus) {
        lcd.setTextColor(C_STATUS, C_PANEL);
        // Up to three 16-char lines of status text
        lcd.setTextDatum(lgfx::top_center);
        lcd.drawString(statusMsg.substring(0, 16).c_str(),  cx, PNL_Y + 18);
        if (statusMsg.length() > 16)
            lcd.drawString(statusMsg.substring(16, 32).c_str(), cx, PNL_Y + 31);
        if (statusMsg.length() > 32)
            lcd.drawString(statusMsg.substring(32, 48).c_str(), cx, PNL_Y + 44);
    } else {
        lcd.setTextColor(C_MUTED, C_PANEL);
        lcd.setTextDatum(lgfx::top_center);
        lcd.drawString("No status set", cx, PNL_Y + 22);
        lcd.drawString("POST /status", cx, PNL_Y + 34);
        if (WiFi.status() == WL_CONNECTED) {
            lcd.setTextColor(C_LABEL, C_PANEL);
            lcd.drawString(WiFi.localIP().toString().c_str(), cx, PNL_Y + 50);
        }
    }
}

static void drawProgress() {
    lcd.fillRect(0, PRG_Y, W, PRG_H, C_BG);

    time_t     epoch = ntp.getEpochTime();
    struct tm *t     = localtime(&epoch);
    int secs = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    int pct  = (int)((long)secs * 100 / 86400);

    const int PAD  = 4;
    const int LW   = 26;
    const int PW   = 34;
    const int bx   = PAD + LW + 3;
    const int bw   = W - bx - PW - PAD;
    const int by   = PRG_Y + (PRG_H - 9) / 2;
    const int bh   = 9;

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
    drawClock();
    drawWeatherPanel();
    drawCryptoPanel();
    drawStatusPanel();
    drawProgress();
    drawDividers();
}

// ── Data fetching ─────────────────────────────────────────────────────────────
static void fetchWeather() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m",
        (float)LATITUDE, (float)LONGITUDE);

    HTTPClient http;
    http.setTimeout(6000);
    http.begin(url);
    bool ok = (http.GET() == HTTP_CODE_OK);
    if (ok) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            tempC    = doc["current"]["temperature_2m"].as<float>();
            wxCode   = doc["current"]["weather_code"].as<int>();
            humidity = doc["current"]["relative_humidity_2m"].as<int>();
            windKmh  = doc["current"]["wind_speed_10m"].as<float>();
        } else {
            ok = false;
        }
    }
    http.end();
    wxFetched = true;
    if (!ok) wxCode = -1;
}

static void fetchCrypto() {
    // Coinbase public API — no key required, not geo-blocked
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(client, "https://api.coinbase.com/v2/prices/BTC-USD/spot");
    bool ok = (http.GET() == HTTP_CODE_OK);
    if (ok) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            // {"data":{"amount":"97234.56","base":"BTC","currency":"USD"}}
            const char* amt = doc["data"]["amount"];
            if (amt) btcUsd = (long)atof(amt);
            else     ok = false;
        } else {
            ok = false;
        }
    }
    http.end();
    btcFetched = true;
    if (!ok) btcUsd = -1;
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
        fetchCrypto();
    }
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static void onRoot() {
    server.send(200, "text/html", R"HTML(<!DOCTYPE html>
<html><head><title>Plusar</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  *{box-sizing:border-box}
  body{background:#06080F;color:#EEF2FF;font-family:monospace;max-width:480px;margin:40px auto;padding:20px}
  h2{color:#33CCFF;margin:0 0 4px}
  p{color:#7A93AC;font-size:12px;margin:0 0 20px}
  input{padding:10px;width:100%;border:1px solid #1E3A52;background:#0B1420;color:#EEF2FF;border-radius:4px;font-size:14px;margin-bottom:8px}
  .row{display:flex;gap:8px}
  button{flex:1;padding:10px;border-radius:4px;cursor:pointer;font-size:14px;border:1px solid #33CCFF;background:#0D1B2A;color:#33CCFF}
  button.warn{border-color:#FF4444;color:#FF4444}
  button:hover{filter:brightness(1.2)}
</style></head>
<body>
<h2>Plusar</h2>
<p>Desk Dashboard — status panel controller</p>
<input id="m" type="text" placeholder="Status message (max 48 chars)..." maxlength="48">
<div class="row">
  <button onclick="set()">Set Status</button>
  <button class="warn" onclick="clr()">Clear</button>
</div>
<script>
function set(){fetch('/status',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:document.getElementById('m').value})});}
function clr(){fetch('/status/clear',{method:'POST'});}
</script>
</body></html>)HTML");
}

static void onStatusPost() {
    String body = server.arg("plain");
    String msg;
    if (body.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, body))
            msg = doc["message"].as<String>();
    }
    if (msg.isEmpty()) msg = server.arg("msg");
    if (msg.isEmpty()) {
        server.send(400, "application/json", R"({"error":"no message"})");
        return;
    }
    statusMsg = msg;
    hasStatus = true;
    drawStatusPanel();
    drawDividers();
    server.send(200, "application/json", R"({"ok":true})");
}

static void onStatusGet() {
    String json = R"({"message":")" + (hasStatus ? statusMsg : String("")) +
                  R"(","set":)" + (hasStatus ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

static void onStatusClear() {
    hasStatus = false;
    statusMsg = "";
    drawStatusPanel();
    drawDividers();
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
        hasStatus = false;
        statusMsg = "";
        drawStatusPanel();
        drawDividers();
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

    clockSprite.setColorDepth(16);
    clockSprite.createSprite(W, CLK_H);

    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString("Connecting...", W / 2, H / 2);

    connectWiFi();

    server.on("/",             HTTP_GET,  onRoot);
    server.on("/status",       HTTP_GET,  onStatusGet);
    server.on("/status",       HTTP_POST, onStatusPost);
    server.on("/status/clear", HTTP_POST, onStatusClear);
    server.begin();

    drawAll();
}

void loop() {
    checkButtons();
    server.handleClient();

    static uint32_t lastSec     = 0;
    static uint32_t lastWeather = 0;
    static uint32_t lastCrypto  = 0;
    static int      lastPct     = -1;
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    uint32_t now    = millis();
    bool     wifiOk = (WiFi.status() == WL_CONNECTED);

    // WiFi reconnect
    if (!wifiOk) {
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
            lastRetry = now;
            connectWiFi();
            wifiOk = (WiFi.status() == WL_CONNECTED);
            if (wifiOk) { drawAll(); wifiWas = true; }
            else          drawHeader();
        }
        return;
    }
    if (!wifiWas) { wifiWas = true; drawAll(); }

    // 1-second tick: clock + conditional header/progress refresh
    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();
        drawClock();

        // Header (date string) refreshes once per minute
        if (ntp.getSeconds() == 0) {
            drawHeader();
            drawDividers();
        }

        // Progress bar redraws only when percentage changes (~once per 14 min)
        time_t     epoch = ntp.getEpochTime();
        struct tm *t     = localtime(&epoch);
        int secs = t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
        int pct  = (int)((long)secs * 100 / 86400);
        if (pct != lastPct) {
            lastPct = pct;
            drawProgress();
        }
    }

    // Weather — every 10 minutes
    if (lastWeather == 0 || now - lastWeather >= 600000UL) {
        lastWeather = now;
        fetchWeather();
        drawWeatherPanel();
        drawHeader();
        drawDividers();
    }

    // Crypto — every 5 minutes
    if (lastCrypto == 0 || now - lastCrypto >= 300000UL) {
        lastCrypto = now;
        fetchCrypto();
        drawCryptoPanel();
        drawDividers();
    }
}
