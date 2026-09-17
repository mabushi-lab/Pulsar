# Pulsar

A real-time market dashboard for the **LilyGo T-Display-S3** (ESP32-S3, 320×170 ST7789 display). Shows 6 EU-listed ETFs and commodity ETCs with live prices, plus a Pomodoro timer, weather, and a Wi-Fi refresh endpoint.

## Hardware

| Component | Detail |
|---|---|
| Board | LilyGo T-Display-S3 |
| MCU | ESP32-S3 |
| Display | 320×170 ST7789, 8-bit parallel bus |
| Library | LovyanGFX 1.1.x |

## Features

- **6-market grid** — 2×3 layout with live price and change %
- **Single-instrument view** — long-press BOOT to toggle a full-screen silver view with large Font7 price
- **Brightness cycling** — short-press BOOT to step through 3 levels (full → dim → off)
- **Session progress bar** — follows whichever instrument is on screen: the shared Euronext day in the grid, and the single instrument's own session (named in the footer) in the large view
- **Pomodoro timer** — full-screen 25-min work / 5-min break mode, entered with the USER button
- **Weather** — current temperature and WMO condition from Open-Meteo (no API key required), shown at the right of the header; a failed fetch shows a muted `wx --` instead of going blank
- **Automatic DST** — the clock uses a POSIX timezone rule, so CET/CEST switches without editing anything
- **Web refresh** — HTTP endpoint at `http://<device-ip>/` with a one-click refresh button
- **Adaptive refresh** — 15 s during continuous trading, 1 min pre/post, 15 min when closed — so the device is not hammering Yahoo overnight
- **Non-blocking fetches** — one symbol per pass, so the clock, buttons and web server stay responsive while prices update
- **Stale-value retention** — a failed request keeps the last good price, dimmed with a small dot, instead of blanking the panel to `Offline`
- **Flash animation** — panels flash blue on price update

## Markets

All 6 instruments are listed on Xetra or Euronext Amsterdam so they share the same trading hours (09:00–17:30 local exchange time).

> **On silver:** `PHAG.AS` is a WisdomTree Physical Silver **ETC listed on Euronext Amsterdam** — not spot silver. Spot silver trades nearly around the clock, but this ETC does not: it stops at 17:30 like everything else in the grid, and its price is frozen outside those hours. The session bar under the large silver view therefore shows Euronext hours, labelled `EURONEXT` so it cannot be misread as a claim about the metal itself.

| Label | Ticker | Instrument | Exchange | TER |
|---|---|---|---|---|
| S&P 500 | SXR8.DE | iShares Core S&P 500 UCITS ETF | Xetra | 0.07% |
| STOXX 50 | EXW1.DE | iShares Core Euro Stoxx 50 UCITS ETF | Xetra | 0.10% |
| Emrg Mkt | EMIM.AS | iShares Core MSCI EM IMI UCITS ETF | Euronext AMS | 0.18% |
| All World | VWCE.DE | Vanguard FTSE All-World UCITS ETF | Xetra | 0.22% |
| Gold | EXS1.DE | iShares Physical Gold ETC | Xetra | 0.12% |
| Silver | PHAG.AS | WisdomTree Physical Silver ETC | Euronext AMS | — |

Prices are fetched from Yahoo Finance (`query2.finance.yahoo.com/v8/finance/chart`). Forex crosses (`=X` symbols) are not supported — Yahoo returns HTTP 401 for those from non-browser clients.

> **Rate limiting:** a refresh cycle is 6 requests. During continuous trading that is ~24 requests/min, which Yahoo may answer with 429. A rate-limited panel keeps its last price, dimmed with a small dot, rather than going blank — so a brief throttle is visible but not destructive. Raise `MKT_REFRESH_OPEN_MS` in `src/config.h` if you see it persistently.

## Trading Sessions

A session is data, not hardcoded hours (`src/config.h`, `src/data.h`). Two are defined:

| Session | Hours (CET/CEST) | Used by |
|---|---|---|
| `SESSION_EQUITY` | 07:00 pre · 09:00–17:30 continuous · 20:00 post-close, weekdays — 42.5 h/week | all 6 instruments |
| `SESSION_METALS` | Sun 23:00 → Fri 22:00, daily break 22:00–23:00 — 115 h/week | none by default |

The footer bar is rendered by sweeping `phaseAtMinute()` across the day, so the bar, the phase label and the polling rate can never disagree, and a near-24h session with a maintenance break draws correctly without special-casing.

`SESSION_METALS` is defined and tested but deliberately unused. Pointing an instrument at it is only correct if you *also* change its symbol to something that genuinely trades around the clock (COMEX `SI=F`, say) — otherwise the bar will claim `OPEN` while the price sits frozen. Note the project's symbols are all cash-market instruments; Yahoo returns HTTP 401 for `=X` forex crosses from non-browser clients, and `=F` futures symbols have not been verified on-device.

Polling follows the fastest session in play (`marketsRefreshMs()`), so adding a 24/5 instrument automatically keeps the refresh rate up overnight.

## Button Controls

**Market view**

| Press | Action |
|---|---|
| BOOT, short (< 600 ms) | Cycle display brightness |
| BOOT, long (≥ 600 ms) | Toggle 6-market grid ↔ silver single view |
| USER, short (< 700 ms) | Enter the Pomodoro timer |
| USER, long (≥ 700 ms) | Manual market refresh |

**Pomodoro view**

| Press | Action |
|---|---|
| USER, short (< 700 ms) | Start / pause / resume the timer |
| USER, long (≥ 700 ms) | Exit back to the market view |
| BOOT, short (< 600 ms) | Cycle display brightness |

A completed work phase auto-starts the 5-minute break; the break ends paused so you choose when the next session begins. Market data is not refreshed while the Pomodoro view is open.

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
```

> `secrets.h` is gitignored and will never be committed.

There is no UTC offset to set. The clock uses the POSIX timezone rule `TZ_INFO` in `src/config.h` (`CET-1CEST,M3.5.0,M10.5.0/3` for Belgium/Luxembourg), so summer time switches itself. Change that string for another region.

### 3. Set your location

Edit `src/config.h` and update the coordinates to your city (and `TZ_INFO` if you are outside Central European Time):

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
  main.cpp       — setup(), loop(), button handling, Pomodoro state, refresh scheduling
  config.h       — pins, layout geometry, colours, timezone, refresh intervals, coordinates
  display.cpp    — LovyanGFX drawing, panel layout, animations
  display.h
  data.cpp       — MarketItem array, fetchMarkets(), fetchWeather(), price formatting
  data.h
  network.cpp    — Wi-Fi, SNTP + timezone, web server (/ and /refresh endpoints)
  network.h
include/
  secrets.h      — Wi-Fi credentials (gitignored)
  secrets.h.example
platformio.ini
```

## Troubleshooting

Both fetchers log to the serial monitor (`pio device monitor`, 115200):

```
[wx]   HTTP 200
[wx]   18.2C  code=3  Overcast
[fetch] SXR8.DE      HTTP 200
           price=592.4400  prev=589.1200
```

- `wx --` in the header means the weather request failed — the `[wx] HTTP <code>` line says why.
- A **dimmed price with a small dot** means the last request for that symbol failed and you are looking at the previous value; the log line says why. `Offline` means that symbol has never fetched successfully.
- `HTTP 429` means Yahoo is rate limiting — raise `MKT_REFRESH_OPEN_MS`.
- `waiting for time sync` in the footer, or `--:--  syncing` in the header, means SNTP has not replied yet; the market phase and polling rate stay unknown until it does.
- The footer in the large single-instrument view is prefixed with that instrument's venue (`EURONEXT`), so the timeline is never ambiguous about which market it describes.
- Both endpoints are HTTPS with certificate verification disabled (`setInsecure()`), which is why no CA bundle is needed.

## License

MIT — see [LICENSE](LICENSE).
