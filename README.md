# Pulsar

A real-time market dashboard for the **LilyGo T-Display-S3** (ESP32-S3, 320×170 ST7789 display). Shows 6 EU-listed ETFs and commodity ETCs with live prices, a Pomodoro timer, weather, and a Wi-Fi refresh endpoint.

## Hardware

| Component | Detail |
|---|---|
| Board | LilyGo T-Display-S3 |
| MCU | ESP32-S3 |
| Display | 320×170 ST7789, 8-bit parallel bus |
| Library | LovyanGFX 1.1.x |

## Features

- **6-market grid** — 2×3 layout with live price, change %, and previous-close reference
- **Single-instrument view** — long-press BOOT to toggle a full-screen silver view with large Font7 price
- **Brightness cycling** — short-press BOOT to step through 4 brightness levels
- **Pomodoro timer** — 25-min work / 5-min break countdown in the header bar
- **Weather** — current temperature and WMO condition from Open-Meteo (no API key required)
- **Web refresh** — HTTP endpoint at `http://<device-ip>/` with a one-click refresh button
- **Auto refresh** — markets re-fetched every 5 minutes while Wi-Fi is connected
- **Flash animation** — panels flash blue on price update

## Markets

All 6 instruments are listed on Xetra or Euronext Amsterdam so they share the same trading hours (09:00–17:30 CET).

| Label | Ticker | Instrument | Exchange | TER |
|---|---|---|---|---|
| S&P 500 | SXR8.DE | iShares Core S&P 500 UCITS ETF | Xetra | 0.07% |
| STOXX 50 | EXW1.DE | iShares Core Euro Stoxx 50 UCITS ETF | Xetra | 0.10% |
| Emrg Mkt | EMIM.AS | iShares Core MSCI EM IMI UCITS ETF | Euronext AMS | 0.18% |
| All World | VWCE.DE | Vanguard FTSE All-World UCITS ETF | Xetra | 0.22% |
| Gold | EXS1.DE | iShares Physical Gold ETC | Xetra | 0.12% |
| Silver | PHAG.AS | WisdomTree Physical Silver ETC | Euronext AMS | — |

Prices are fetched from Yahoo Finance (`query2.finance.yahoo.com/v8/finance/chart`). Forex crosses (`=X` symbols) are not supported — Yahoo returns HTTP 401 for those from non-browser clients.

## Button Controls

| Press | Action |
|---|---|
| Short press BOOT (< 600 ms) | Cycle display brightness |
| Long press BOOT (≥ 600 ms) | Toggle 6-market grid ↔ silver single view |
| USER button | Manual market refresh |

## Setup

### 1. Install PlatformIO

```bash
pip install platformio
# or use the PlatformIO IDE extension in VS Code
```

### 2. Configure secrets

Copy the template and fill in your values:

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#define WIFI_SSID       "YourNetwork"
#define WIFI_PASSWORD   "YourPassword"

// UTC offset in seconds
// UTC+1 CET  (winter): 3600
// UTC+2 CEST (summer): 7200
#define UTC_OFFSET_SEC  7200
```

> `secrets.h` is gitignored and will never be committed.

### 3. Set your weather location

Edit `src/config.h` and update the coordinates to your city:

```cpp
const float WEATHER_LAT = 50.8798f;  // latitude
const float WEATHER_LON =  4.7005f;  // longitude
```

Find your coordinates at [latlong.net](https://www.latlong.net).

### 4. Build and flash

```bash
pio run -t upload
pio device monitor
```

The board must be in bootloader mode for the first flash: hold **BOOT**, press **RST**, then release **BOOT** before running the upload command.

## Project Structure

```
src/
  main.cpp       — setup(), loop(), button handling, refresh scheduling
  display.cpp    — LovyanGFX drawing, panel layout, animations
  display.h
  data.cpp       — MarketItem array, fetchMarkets(), fetchWeather(), price formatting
  data.h
  network.cpp    — Wi-Fi, NTP, web server (/refresh endpoint)
  network.h
include/
  secrets.h      — Wi-Fi credentials, UTC offset, coordinates (gitignored)
platformio.ini
```

## DST Note

`UTC_OFFSET_SEC` in `secrets.h` must be updated manually when daylight saving changes:
- **Summer (CEST, last Sunday March → last Sunday October):** `7200`
- **Winter (CET):** `3600`

## License

MIT — see [LICENSE](LICENSE).
