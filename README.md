# Pulsar

A portfolio dashboard for the **LilyGo T-Display-S3** (ESP32-S3, 320x170 ST7789). It tracks your own positions with live public quotes, converts everything into one currency, and keeps a daily record of what the portfolio is worth.

## Hardware

| Component | Detail |
|---|---|
| Board | LilyGo T-Display-S3 |
| MCU | ESP32-S3 |
| Display | 320x170 ST7789, 8-bit parallel bus |
| Library | LovyanGFX 1.1.x |

## What it shows

Five screens at four stops on the **BOOT** long-press cycle:

- **Positions** - your holdings in a 2x3 grid with live price and day change, paged if you hold more than six
- **Detail** - one position full-screen: price, day change, units and total return. The day figure says **last close** when that instrument's own venue is shut, which is not the same question as whether the portfolio's is
- **Portfolio** - **total return** as the headline figure, with the day's move beneath it as context; total value sits in the corner. Amounts can be hidden if the screen is somewhere public. When every venue you hold is shut, the day figure says **last close** rather than **today**. A 7- and 30-day return sits below that, once daily history actually reaches back that far - see [Value history](#value-history) below
- **Loan** - for money that was borrowed to invest: what has been drawn, what it has cost in interest, what it bought is worth, and **net equity** as the headline
- **Allocation** - *reached with USER from the Loan screen* - each targeted fund as a bar against its target, drift in percentage points, and the amount that would close the worst gap

Allocation and Loan share a stop on the cycle because they describe the same thing from two sides - the borrowed, targeted sleeve of funds. One says what it owes, the other whether it is balanced. USER flips between them.

The header carries the clock, the date, the link state and your **portfolio day change**, so the number you most want is on screen in every view. The footer is a trading-session bar for whichever venue is relevant, with a live time cursor and time-to-next-phase.

## Loan-funded investing

Part of this portfolio can be bought with borrowed money - a loan drawn in equal tranches at a fixed interval, charged a fixed annual rate that capitalises onto the balance. A portfolio screen that ignores that flatters the position by exactly the amount owed, and the gap widens every month.

The loan view answers three things: **how much has been drawn**, **what it has cost so far in interest**, and **what the investments it bought are worth**. The headline figure is neither of the first two - it is **net equity**, value minus what it would cost to repay today, because that is what would actually be left on liquidation.

Underneath sits the **spread**: the return the money achieved against the rate it costs to borrow. That is the number that says whether the leverage is doing its job, and it is the one that turns negative first.

The return is a money-weighted rate solved against the *same drawdown schedule* the interest is charged on, so the two figures are directly comparable - comparing a simple total return against a compounding loan rate would flatter whichever side had its money in longest. It assumes each tranche was invested when it was drawn.

Configure it under **Loan** in Settings: first drawdown date, amount, interval, total number of drawdowns, rate, and which symbols the loan funded. Anything left out of that symbol list is treated as your own money and stays out of every loan figure.

**The first drawdown date ships empty and the view stays inert until you set it.** There is no sane default for it - everything accrues from that date - and a guessed date would report wrong money without ever looking wrong.

## Positions and watchlist

One list, one editor, at `http://pulsar.local/`:

```
SXR8.DE,12.5,540.20,S&P 500,50
PPFD.SG,62.388935,51.59,Silver
AAPL,0,0,Apple
```

`SYMBOL,QUANTITY,AVG COST` with an optional short label and an optional **target allocation** in percent. **A quantity of 0 makes it a watchlist entry** - priced and displayed like any other, but excluded from every total.

Use the Yahoo Finance symbol for the listing you actually bought. Positions live in NVS flash, so a trade means editing a form, not reflashing.

> **Use a dot for decimals.** The comma separates fields, so `51,59` is rejected with an explanation rather than silently read as `51`. A purely numeric label is rejected for the same reason, and a label may not contain `<` - it is later written straight into the web app's own page, and that is the one character that could hijack it.

### Currency, and what the return figure actually means

Everything is converted into the base currency with ECB reference rates, so a mixed EUR/USD portfolio totals correctly rather than adding numbers that are not comparable. Conversion goes through the rate table's own base, which is taken from the provider's response rather than assumed, so it stays correct even if the provider ignores the requested base.

One property worth understanding rather than discovering. Average cost is entered in the instrument's own currency, and the cost basis is converted at **today's** rate - the device is never told what the rate was on the day you bought. Both legs of the return therefore carry the same rate and it cancels, which means **the reported return for a foreign holding is that asset's return in its own currency, not your return in euros including the currency move.**

For a fund bought at $200 and now worth $250, the device says +25% whatever the dollar has done since. If the dollar fell 8% against the euro over that period, your actual euro return was nearer +15%. The device is not wrong, it is answering "how has this asset done" rather than "how much money have I made", and it cannot answer the second without your purchase-date exchange rates.

This makes no difference to a single-currency portfolio, where the two questions have the same answer.

### Prices you can trust

Two checks guard against confidently wrong figures rather than merely missing ones:

- **Implausible moves.** A one-day move past 25% is flagged as a probable split or other corporate action. A 4:1 split drops the price by 75% while your quantity should have quadrupled; a tracker that is not told reports a catastrophic loss with total confidence. Broad funds do not move 25% in a day, so when the number says they did, the number is wrong. This warning outranks everything else on the portfolio screen.
- **Quotes from different sessions.** Each price carries the venue's own timestamp. When the prices being summed are more than 36 hours apart, they are not all from the same session and the day change is blending more than one day - usually a venue holiday, or a symbol that has stopped trading. The dashboard says so instead of adding them up quietly.

### Why there is no broker integration

**Trade Republic has no public API.** No developer portal, no OAuth, no read-only tokens - every library you will find ([pytr](https://github.com/nborrmann/pytr), [TradeRepublicApi](https://github.com/Zarathustra2/TradeRepublicApi)) is an unofficial client for the private mobile-app API. They need your phone number, PIN and 2FA, and pytr's setup performs a device reset that logs you out of your phone. That does not belong in flash on a desk gadget, and it breaks whenever the app changes.

So this is a local model instead: you own the position data, the device fetches only public prices, and no credentials exist anywhere in the project. The cost is that you update it by hand after a trade.

## Web app

The device advertises itself over mDNS, so **you do not need its IP**:

```
http://pulsar.local/
```

The address is also shown on screen for a few seconds at boot and printed to the serial monitor.

| Page | Contents |
|---|---|
| `/` | Totals, allocation, fetch diagnostics, per-position status, positions editor |
| `/settings` | Everything below, applied live |

Set `WEB_PASSWORD` in `include/secrets.h` to put the whole web app behind a password (username `pulsar`, or set `WEB_USER`). It is off by default: a prompt nobody asked for is a bad default for a device on your own desk. It guards reads as well as writes on purpose — guarding only the writes would leave the dashboard's Refresh button silently returning 401 from a page the browser was never challenged on.

**Settings**: orientation, brightness, night dimming, default view, amount visibility, base currency, timezone, exchange-rate URL, the three refresh intervals, the loan schedule, an optional alert webhook, plus reset-to-defaults and clear-history.

**Night dimming** drops the backlight between two wall-clock times — 23:00 to 07:00 by default. The window is local time, so it follows the same timezone rule as the clock through daylight saving, and a window that runs past midnight is the normal case rather than an edge one. A BOOT press still overrides the level by hand; the schedule only takes over again at the next dusk or dawn, so a deliberate choice is not undone a second later.

**Amount visibility** has three modes. *Always* (the default) puts today's move in money on screen with the percentage beside it — a percentage alone hides magnitude, and 1% of a small position reads identically to 1% of a large one. *Reveal on press* keeps percentages on screen and shows money for six seconds after a USER tap. *Never* keeps amounts off the device entirely. The middle and last modes exist because a desk display is readable by anyone walking past. Orientation and brightness repaint immediately; a timezone change re-applies the rule on the spot; a base-currency change refetches rates.

Every field is validated - the first bad value aborts the save so nothing is left half-applied, and an out-of-range stored value is repaired on load rather than trusted.

### The seven-segment face

The large figures are drawn in LovyanGFX's `Font7`, which contains exactly the digits, `-`, `.`, `:` and space. Its own header says *"All other characters print as a space"* - so a hero built with the ordinary formatters loses its `+`, its thousands separator and its `%` somewhere between the number and the glass, and the resulting blanks push a centred string off centre. `fmtSevenSeg()` maps a figure into the alphabet the font actually has instead of letting the font edit it: a separator becomes a space, which is a real thousands separator across most of Europe and one it can draw; a leading `+` is dropped so positives stay centred; the `-` survives, because that glyph exists.

## Buttons

| Press | Action |
|---|---|
| BOOT, short | Cycle brightness (full / dim / off), overriding the night schedule until it next changes |
| BOOT, long | Next view |
| USER, short | "Next" in the current view: next page, next position, reveal amounts, or flip between Loan and Allocation |
| USER, long | Refresh now |

## How it fetches

A **watchdog** reboots the device if a loop pass stalls for 45 seconds - long enough that no legitimate request trips it, short enough that a hung TLS handshake becomes a restart rather than a frozen screen that still looks powered. The boot counter on the web page is how you notice it happened.

A refresh **cycle** makes one HTTPS request per position, one per `loop()` pass, so the clock, buttons and web server stay responsive throughout - rather than freezing for the length of the whole burst.

Polling follows the trading session of the most active position: fast during continuous trading, slower pre/post, slowest when everything is shut. A position's session is inferred from its venue suffix (`.DE`, `.AS`, `.SG` and friends are European, a bare ticker is US, `=F` is a future); a wrong guess only affects the footer label and the polling rate, never a price.

A failed request **keeps the last good price**, dimmed with a dot, rather than blanking the panel. `no price` means that symbol has never fetched successfully.

Quotes are fetched over TLS without certificate verification. That is deliberate: pinning a root CA turns a routine certificate rotation into a device that silently stops updating, and what crosses the connection is a public share price with no credentials attached. The worst a successful intercept buys is a wrong number on a desk display.

Symbols are checked before they are saved - letters, digits, `.`, `-`, `^` and `=` only. They go straight into the quote URL, so a stray `?` or `&` would rewrite the request's query string and surface only as a puzzling HTTP error against a symbol that looks fine on screen.

### Venues

A position's trading session comes from its venue suffix, and the hours are the venue's real ones rather than one European default:

| Suffix | Session | Hours (local) |
|---|---|---|
| `.DE` | Xetra | 09:00-17:30 |
| `.SG` `.F` `.BE` `.MU` `.DU` `.HM` | German regional | 08:00-22:00 |
| `.AS` `.PA` `.BR` `.LS` | Euronext | 09:00-17:30 |
| `.L` | LSE | 09:00-17:30 CET |
| other European | Europe | 09:00-17:30 |
| bare ticker | US | 15:30-22:00 CET |
| `=F` | COMEX | near-24/5 |

This is not cosmetic. The silver ETC trades on Stuttgart until 22:00; treating it as a Xetra listing dropped it to 15-minute polling from 17:30 and named the wrong venue in the footer.

### Flash

History lives in NVS, and NVS is flash. A cycle completes every 15 seconds while a market is open and each one has a snapshot for today, so persisting every one would rewrite the whole blob about 2,000 times a day - enough to wear the partition out inside a year. The point in RAM is updated every time; the write is throttled to once every ten minutes and forced whenever a new day is appended or the device is about to reboot for an update. The web app reports the write count for the current boot.

### Value history

One value-and-cost snapshot is recorded per calendar day, which is what the Portfolio screen's 7- and 30-day return is drawn from. It looks up the closest recorded day *at or before* the target date rather than an exact match, because a device that was off for a stretch has ordinary gaps in the series - but a gap wider than the window itself is declined rather than mislabelled: a 7-day figure is never quietly answered with a point that is actually three weeks old. Until the series reaches back that far, the line simply does not appear.

### Allocation and drift

Every column of the allocation table shares one denominator - the targeted sleeve - because showing share-of-everything beside a target defined over the funds alone put three mutually contradictory numbers on one line.

Drift is measured **within the targeted sleeve**, not against the whole portfolio. A position with no target - physical silver here - is separate money and does not dilute an allocation defined over the funds that name it. Measuring against the whole book instead made every targeted fund read permanently short by exactly the untargeted share, and the rebalance hint named the same fund forever regardless of what was held.

Targets are renormalised by their own sum, so a list adding to 95 or 105 still compares sanely instead of showing a constant offset on every row.

The bottom line gives the amount that would bring the worst-drifted fund back to target, in the base currency when amounts are visible. The web app lists that amount per fund.

The device screen only has room to draw six funds; a targeted sleeve larger than that shows "N of M funds" so the gap is visible rather than silent, and the rebalance hint is always computed over every targeted fund, not just the ones drawn - a fund that does not fit on screen can still be the one it names. The web app's own Allocation table has no such limit and always lists every targeted fund.

### Catching a bad setup

The dashboard warns, in place rather than in a diagnostics table, when a symbol in the loan list matches no position. A typo there silently drops a holding from every loan figure while the screen goes on looking entirely healthy - which is this device's worst failure mode and the one it has produced most often. It also flags targets that add up far from 100.

### Alerts

Optional, and off unless a webhook URL is set under **Alerts** in Settings. When set, the device posts a `{"text": "..."}` body - the shape Slack's incoming webhooks expect, and a form most self-hosted relays accept - the moment one of the conditions above starts or clears: a probable stock split, a loan symbol matching no position, or a fund drifted past the same threshold the Portfolio and Allocation screens already draw in orange. Each fires once at the change, not on every refresh cycle, so an unresolved condition does not turn into a ping every fifteen seconds. A **Send test alert** button next to the field posts a fixed message immediately, so setting the URL up does not mean waiting for a real warning to find out it works.

Nothing quantitative ever goes in a message - no price, no value, no position size - only which condition changed and a fund's own label. The connection is unverified TLS, the same posture as every other outbound request this device makes (see [Why there is no broker integration](#why-there-is-no-broker-integration) and the note on quote fetching below): what it could leak is already visible in full to anyone with LAN access to the web dashboard, which is unauthenticated by default.

## Setup

### 1. Install PlatformIO

```bash
pip install platformio
```

### 2. Wi-Fi credentials

```bash
cp include/secrets.h.example include/secrets.h
```

```cpp
#define WIFI_SSID       "YourNetwork"
#define WIFI_PASSWORD   "YourPassword"
```

Set `OTA_PASSWORD` in the same file if you want authenticated over-the-air updates — see below.

`secrets.h` is gitignored. There is no UTC offset to set: the clock uses the POSIX timezone rule in Settings, so summer time switches itself.

### 3. Build and flash

```bash
pio run -t upload
pio device monitor
```

Hold **BOOT**, press **RST**, release **BOOT** before the first upload.

### 4. Updating over Wi-Fi

After the first USB flash the device listens for firmware pushes, so the cable is only needed once:

```bash
PULSAR_OTA_PASSWORD=yourpassword pio run -e ota -t upload
```

`platformio.ini` carries a separate `ota` environment for this, so USB uploads keep working untouched and nothing needs editing before each push. The password comes from the environment rather than the file, because `platformio.ini` is committed - putting it there would undo the point of keeping it in the gitignored `secrets.h`. It must match `OTA_PASSWORD` in `secrets.h`.

If `pulsar.local` does not resolve, use the IP instead - it is on the boot screen and on the web dashboard:

```bash
PULSAR_OTA_PASSWORD=yourpassword pio run -e ota -t upload --upload-port 192.168.1.42
```

The screen shows a progress bar during the transfer and an error with a code if it fails, so a device mid-update never just looks dead. The watchdog is disabled for the duration and today's history point is flushed to flash first, since the device is about to reboot.

The board's partition table carries two 6.25 MB application slots and an `otadata` partition. The new firmware is written into the inactive slot and only becomes active once its hash verifies, so a failed or interrupted push leaves the running firmware untouched - worst case you pull the cable and flash over USB.

**Set `OTA_PASSWORD` in `include/secrets.h`.** Without it the update endpoint still works but accepts anyone on the network, which means anyone on the network can replace this firmware entirely. The device's own web page says so, under *Firmware update*, until you set one.

## Troubleshooting

Everything logs to the serial monitor at 115200:

```
[cfg]   rot=0 bri=255 view=0 base=EUR tz=CET-1CEST,M3.5.0,M10.5.0/3
[pf]    loaded 3 position(s), 2 held
[fx]    4 rate(s), base EUR, dated 2026-09-18
[fetch] SXR8.DE        HTTP 200
          price=592.4400 prev=589.1200 EUR
[cyc]   cycle 1 complete (3 position(s))
[hist]  day 2452 recorded: value=12480.22 cost=10900.00 (37 stored)
[ota]   ready: pio run -t upload --upload-port pulsar.local
```

The **Fetching** section of the web app shows the same state without a serial cable: whether a cycle is running and where, cycles completed, time since the last one, the current interval, FX rate state, history depth and flash writes, OTA status, free heap, uptime and **boot count**.

- **Free heap falling steadily** over days points at fragmentation from the per-request TLS buffers, which ends with every fetch failing at once rather than gradually. The low-water figure beside it is the number that shows the trend; the watchdog and boot counter are what catch it if it gets that far.

- A boot count climbing while uptime stays low means the device is **restarting in a loop** - which looks identical, from the outside, to a device that is merely slow to fetch. That ambiguity is what made an unfetched position hard to diagnose, so the counter is there to settle it.

- `not fetched yet` next to a position, with **cycles completed: 0**, means nothing is fetching at all - check Wi-Fi and uptime.
- `no price - HTTP 404` means Yahoo does not serve that symbol on that venue. Check it resolves at `finance.yahoo.com/quote/<SYMBOL>`.
- `HTTP 429` means rate limiting - raise the open-market interval in Settings.
- `some positions excluded - no FX rate` means the rates table has no entry for a position's currency; check the FX line in the diagnostics.
- An **OTA push that cannot find the device** usually means mDNS is not resolving; use the IP shown in the diagnostics instead of `pulsar.local`. `Authentication Failed` means `OTA_PASSWORD` and `--auth=` disagree. mDNS and OTA start as soon as Wi-Fi comes up, whether that happens within the boot splash's wait or later — a device that came up before its own router did no longer needs a restart once it joins the network.
- **The screen is dark at night and you did not expect it**: night dimming is on by default from 23:00 to 07:00. Turn it off, or move the window, in Settings. A BOOT press brings it back until the next scheduled change.

## Project structure

```
src/
  main.cpp       - setup(), loop(), buttons, refresh and history scheduling
  config.h       - pins, layout, colours, sessions, defaults
  portfolio.cpp  - positions, totals, NVS persistence
  portfolio.h
  data.cpp       - trading sessions, quote fetching, the refresh cycle
  data.h
  fx.cpp         - ECB rates, caching, currency conversion
  fx.h
  loan.cpp       - drawdown schedule, capitalised interest, net equity
  loan.h
  history.cpp    - daily value snapshots, the 7/30-day return lookup
  history.h
  alerts.cpp     - optional webhook alerts on split/loan-typo/drift conditions
  alerts.h
  display.cpp    - LovyanGFX drawing, the five screens, night dimming, OTA screen
  display.h
  network.cpp    - Wi-Fi, mDNS, SNTP, OTA, web app
  network.h
  settings.cpp   - runtime settings, validation, NVS
  settings.h
include/
  secrets.h      - Wi-Fi credentials, OTA and web passwords (gitignored)
  secrets.h.example
platformio.ini
```

## License

MIT - see [LICENSE](LICENSE).
