// Plusar — Desk Status Board
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUDP.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ── Display ───────────────────────────────────────────────────────────────────
// Canonical 8-bit parallel config for T-Display S3 (from LILYGO official repo)
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
            cfg.offset_rotation = 1;  // panel default = landscape (320×170)
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
static const int W         = 320;
static const int H         = 170;
static const int HEADER_H  = 18;
static const int CLOCK_Y   = 28;
static const int CLOCK_H   = 52;  // sprite height; Font7 = 48 px
static const int STATUS_Y  = 100;
static const int STATUS_H  = 32;
static const int FOOTER_Y  = 148;

// ── Colours ───────────────────────────────────────────────────────────────────
static const uint32_t C_BG         = 0x000000;
static const uint32_t C_HEADER_BG  = 0x0D1B2A;
static const uint32_t C_STATUS_BG  = 0x0A1628;
static const uint32_t C_CLOCK      = 0xFFFFFF;
static const uint32_t C_DATE       = 0x9AAABB;
static const uint32_t C_WEATHER    = 0xFFAA33;
static const uint32_t C_STATUS     = 0x33CCFF;
static const uint32_t C_STATUS_OFF = 0x446677;
static const uint32_t C_DIVIDER    = 0x1E3A52;
static const uint32_t C_FOOTER     = 0x445566;
static const uint32_t C_WIFI_OK    = 0x00DD55;
static const uint32_t C_WIFI_ERR   = 0xFF4444;

// ── State ─────────────────────────────────────────────────────────────────────
static String statusMsg  = "";
static bool   hasStatus  = false;
static float  tempC      = 0.0f;
static int    wxCode     = -1;
static bool   fullBright = true;

// ── NTP / web server ──────────────────────────────────────────────────────────
static WiFiUDP   ntpUdp;
static NTPClient ntp(ntpUdp, "pool.ntp.org", UTC_OFFSET_SEC);
static WebServer server(80);

// ── WMO weather code descriptions ─────────────────────────────────────────────
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

// ── Rendering ─────────────────────────────────────────────────────────────────

static void drawHeader() {
    lcd.fillRect(0, 0, W, HEADER_H, C_HEADER_BG);

    time_t      epoch = ntp.getEpochTime();
    struct tm  *t     = localtime(&epoch);
    char        dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%a, %b %d %Y", t);

    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(C_DATE, C_HEADER_BG);
    lcd.setTextDatum(lgfx::top_left);
    lcd.drawString(dateBuf, 5, 3);

    // WiFi status dot (top-right corner)
    bool ok = (WiFi.status() == WL_CONNECTED);
    lcd.fillCircle(W - 9, 9, 4, ok ? C_WIFI_OK : C_WIFI_ERR);

    // Weather (centred — only once first fetch succeeds)
    if (wxCode >= 0) {
        char wx[48];
        snprintf(wx, sizeof(wx), "%.0f%cC  %s", tempC, (char)0xB0, wmoDesc(wxCode));
        lcd.setTextColor(C_WEATHER, C_HEADER_BG);
        lcd.setTextDatum(lgfx::top_center);
        lcd.drawString(wx, W / 2, 3);
    }
}

static void drawClock() {
    clockSprite.fillSprite(C_BG);
    clockSprite.setFont(&fonts::Font7);
    clockSprite.setTextSize(1);
    clockSprite.setTextColor(C_CLOCK);
    clockSprite.setTextDatum(lgfx::middle_center);

    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             ntp.getHours(), ntp.getMinutes(), ntp.getSeconds());

    clockSprite.drawString(buf, W / 2, CLOCK_H / 2);
    clockSprite.pushSprite(0, CLOCK_Y);
}

static void drawStatus() {
    lcd.fillRect(0, STATUS_Y, W, STATUS_H, C_STATUS_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::middle_center);

    if (hasStatus) {
        lcd.setTextColor(C_STATUS, C_STATUS_BG);
        lcd.drawString(statusMsg.c_str(), W / 2, STATUS_Y + STATUS_H / 2);
    } else {
        lcd.setTextColor(C_STATUS_OFF, C_STATUS_BG);
        lcd.drawString("Push a status via curl or browser", W / 2, STATUS_Y + STATUS_H / 2);
    }
}

static void drawFooter() {
    lcd.fillRect(0, FOOTER_Y, W, H - FOOTER_Y, C_BG);
    lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextDatum(lgfx::bottom_right);

    if (WiFi.status() == WL_CONNECTED) {
        lcd.setTextColor(C_FOOTER, C_BG);
        lcd.drawString(WiFi.localIP().toString().c_str(), W - 4, H - 2);
    } else {
        lcd.setTextColor(C_WIFI_ERR, C_BG);
        lcd.drawString("WiFi connecting...", W - 4, H - 2);
    }
}

static void drawDividers() {
    lcd.drawFastHLine(0, HEADER_H,     W, C_DIVIDER);
    lcd.drawFastHLine(0, STATUS_Y - 2, W, C_DIVIDER);
    lcd.drawFastHLine(0, FOOTER_Y - 2, W, C_DIVIDER);
}

static void drawAll() {
    lcd.fillScreen(C_BG);
    drawDividers();
    drawHeader();
    drawClock();
    drawStatus();
    drawFooter();
}

// ── Weather fetch (Open-Meteo, no API key needed) ─────────────────────────────
static void fetchWeather() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code",
        (float)LATITUDE, (float)LONGITUDE);

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(url);

    if (http.GET() == HTTP_CODE_OK) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            tempC  = doc["current"]["temperature_2m"].as<float>();
            wxCode = doc["current"]["weather_code"].as<int>();
        }
    }
    http.end();
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
    }
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static void onRoot() {
    server.send(200, "text/html", R"(<!DOCTYPE html>
<html><head><title>Plusar</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  body{font-family:monospace;max-width:420px;margin:40px auto;padding:20px}
  input,button{padding:8px;margin:4px 0;width:100%;box-sizing:border-box}
  button{cursor:pointer}
</style></head><body>
<h2>Plusar</h2>
<input id="m" type="text" placeholder="Status message...">
<button onclick="set()">Set Status</button>
<button onclick="clr()">Clear</button>
<script>
function set(){fetch('/status',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({message:document.getElementById('m').value})});}
function clr(){fetch('/status/clear',{method:'POST'});}
</script></body></html>)");
}

static void onStatusPost() {
    String msg;

    String body = server.arg("plain");
    if (body.length() > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, body))
            msg = doc["message"].as<String>();
    }
    if (msg.isEmpty())
        msg = server.arg("msg");

    if (msg.isEmpty()) {
        server.send(400, "application/json", R"({"error":"no message"})");
        return;
    }

    statusMsg = msg;
    hasStatus = true;
    drawStatus();
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
    drawStatus();
    server.send(200, "application/json", R"({"ok":true})");
}

// ── Button handling ───────────────────────────────────────────────────────────
static void checkButtons() {
    static bool     prevBoot = HIGH, prevUser = HIGH;
    static uint32_t lastMs   = 0;

    if (millis() - lastMs < 50) return;
    lastMs = millis();

    bool boot = digitalRead(BTN_BOOT);
    bool user = digitalRead(BTN_USER);

    if (prevBoot == HIGH && boot == LOW) {
        fullBright = !fullBright;
        lcd.setBrightness(fullBright ? 255 : 50);
    }
    if (prevUser == HIGH && user == LOW) {
        hasStatus = false;
        statusMsg = "";
        drawStatus();
    }

    prevBoot = boot;
    prevUser = user;
}

// ── Entry points ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Must pull high before display init or the screen stays dark
    pinMode(PIN_POWER, OUTPUT);
    digitalWrite(PIN_POWER, HIGH);

    pinMode(BTN_BOOT, INPUT_PULLUP);
    pinMode(BTN_USER, INPUT_PULLUP);

    lcd.init();
    lcd.setRotation(0);     // offset_rotation=1 in panel config → this gives landscape
    lcd.setBrightness(255);

    clockSprite.setColorDepth(16);
    clockSprite.createSprite(W, CLOCK_H);

    // Splash while connecting
    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextDatum(lgfx::middle_center);
    lcd.setTextColor(C_DATE, C_BG);
    lcd.drawString("Connecting to WiFi...", W / 2, H / 2);

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
    static bool     wifiWas     = (WiFi.status() == WL_CONNECTED);

    uint32_t now    = millis();
    bool     wifiOk = (WiFi.status() == WL_CONNECTED);

    if (!wifiOk) {
        static uint32_t lastRetry = 0;
        if (now - lastRetry > 30000) {
            lastRetry = now;
            connectWiFi();
            wifiOk = (WiFi.status() == WL_CONNECTED);
            if (wifiOk) { drawAll(); wifiWas = true; }
            else          drawFooter();
        }
        return;
    }

    if (!wifiWas) {
        wifiWas = true;
        drawAll();
    }

    if (now - lastSec >= 1000) {
        lastSec = now;
        ntp.update();
        drawClock();

        // Refresh the header (WiFi dot + date) once per minute
        if (ntp.getSeconds() == 0) {
            drawHeader();
            drawDividers();
        }
    }

    if (lastWeather == 0 || now - lastWeather >= 600000UL) {
        lastWeather = now;
        fetchWeather();
        drawHeader();
        drawDividers();
    }
}
