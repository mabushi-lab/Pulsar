#include "network.h"
#include "config.h"
#include "portfolio.h"
#include "settings.h"
#include "display.h"
#include "data.h"
#include "fx.h"
#include "history.h"
#include "loan.h"
#include "secrets.h"
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include <math.h>
#include <string.h>

// secrets.h is the user's own file and is gitignored, so a device flashed from
// an older copy of it has no OTA_PASSWORD. Rather than failing to build, that
// case runs without one and says so loudly — on the serial log and on the web
// page — because an unauthenticated OTA endpoint lets anyone on the network
// replace this firmware entirely.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

// Optional protection for the whole web app. Off unless WEB_PASSWORD is set in
// secrets.h, because a password prompt nobody asked for is a bad default for a
// device on your own desk.
//
// It guards reads as well as writes, deliberately. Guarding only the writes
// sounds friendlier, but the dashboard's Refresh button is a fetch() to a
// guarded route from a page the browser was never challenged on: it would get a
// 401 and do nothing at all, with no prompt to explain why. If you set a
// password you want the device protected, so this protects all of it.
//
// This is HTTP, so the credentials cross the LAN base64-encoded, not
// encrypted. It is a guard against a housemate and a mis-click, not against
// someone already reading your traffic.
#ifndef WEB_USER
#define WEB_USER "pulsar"
#endif
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""
#endif

// ── Globals ───────────────────────────────────────────────────────────────────
WebServer server(80);
bool      refreshRequested  = false;
bool      fxFetchRequested  = false;

// ── HTTP handlers ─────────────────────────────────────────────────────────────
// ── Chunked-response guard ────────────────────────────────────────────────────
// With setContentLength(CONTENT_LENGTH_UNKNOWN) the response is chunked, and
// sendContent() writes a chunk header carrying the content's length. A
// zero-length chunk is HTTP's end-of-response marker: it terminates the reply
// and clears the server's chunked flag, so anything written afterwards is
// discarded by the browser. portfolioSerialize() returns "" when no holdings
// are stored, which ended the page right after the <textarea> — no Save
// button, no closing tags — on exactly the fresh device every new user starts
// from. Every write goes through sendChunk(); only endChunked() may send empty.
static void sendChunk(const char* c) {
    if (c && c[0]) server.sendContent(c);
}

static void endChunked() { server.sendContent(""); }

// Everything below goes into an HTML page built with snprintf, not a template
// engine, so nothing escapes '<' on its own. Labels are free text a user types
// into the positions editor, and currency comes from the quote provider over a
// connection this project deliberately does not certificate-pin (see README) —
// both are attacker-reachable, on a page that is unauthenticated by default.
// Every such string is run through this before it reaches a page buffer.
static void htmlEscape(char* out, size_t outSize, const char* in) {
    if (!out || !outSize) return;
    size_t o = 0;
    for (const char* c = in ? in : ""; *c && o + 6 < outSize; c++) {
        switch (*c) {
            case '&':  memcpy(out + o, "&amp;",  5); o += 5; break;
            case '<':  memcpy(out + o, "&lt;",   4); o += 4; break;
            case '>':  memcpy(out + o, "&gt;",   4); o += 4; break;
            case '"':  memcpy(out + o, "&quot;", 6); o += 6; break;
            case '\'': memcpy(out + o, "&#39;",  5); o += 5; break;
            default:   out[o++] = *c; break;
        }
    }
    out[o] = '\0';
}

// Returns false having already answered the request, so a handler's first line
// can be `if (!mayAccess()) return;`.
static bool mayAccess() {
    if (!WEB_PASSWORD[0]) return true;
    if (server.authenticate(WEB_USER, WEB_PASSWORD)) return true;
    server.requestAuthentication();
    return false;
}

// The page is assembled in parts because it now carries the holdings editor;
// each snprintf is bounded by its own buffer and the result is streamed out, so
// nothing can be silently truncated the way the old single-buffer version was.
static void onRoot() {
    if (!mayAccess()) return;
    static const char HEAD[] = R"HTML(<!DOCTYPE html>
<html><head><title>Pulsar</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#06080F;color:#EEF2FF;font-family:monospace;max-width:600px;margin:32px auto;padding:20px}
h2{color:#33CCFF;margin-bottom:4px}p{color:#7A93AC;font-size:13px;line-height:1.5}
textarea{width:100%;height:190px;background:#0B1420;color:#EEF2FF;border:1px solid #1E3A52;
border-radius:4px;padding:10px;font-family:monospace;font-size:13px;box-sizing:border-box}
button{padding:10px 20px;margin-top:10px;background:#0D1B2A;border:1px solid #33CCFF;color:#33CCFF;
border-radius:4px;cursor:pointer;font-size:14px}
button:hover{filter:brightness(1.3)}
.note{color:#3D5A73;font-size:12px}
table{width:100%;border-collapse:collapse;font-size:13px;margin-top:8px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #1E3A52}
th{color:#3D5A73;font-weight:normal}
.good{color:#00CC55}.warn{color:#FFAA33}.bad{color:#FF3344}.dim{color:#3D5A73}
.num{text-align:right}
hr{border:0;border-top:1px solid #1E3A52;margin:24px 0}
a{color:#33CCFF}</style></head>
<body><h2>Pulsar Portfolio Dashboard</h2>
)HTML";

    static const char FORM[] = R"HTML(<hr>
<h2>Positions</h2>
<p>One per line: <b>SYMBOL,QUANTITY,AVG&nbsp;COST</b> with an optional short label.<br>
<span class="note">SXR8.DE,12.5,540.20,S&amp;P 500,50</span><br>
A <b>quantity of 0</b> makes it a watchlist entry: priced and shown on the device,
but left out of every total. An optional fifth field sets a <b>target allocation</b>
in percent, which drives the drift figures above. Use a dot for decimals &mdash;
the comma separates fields, and a label may not contain &lt;.<br>
Nothing is sent anywhere; positions are stored on the device and priced with public quotes.</p>
<form method="POST" action="/positions">
<textarea name="h" spellcheck="false">)HTML";

    static const char TAIL[] = R"HTML(</textarea>
<button type="submit">Save positions</button>
</form>
<p class="note">Max %d positions. Values are converted into %s using ECB reference rates.</p>
</body></html>)HTML";

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    sendChunk(HEAD);

    // Static rather than stack-local: onRoot() only ever runs synchronously
    // from server.handleClient() on the loop task, never reentrantly, and its
    // scratch buffers alone summed to over 3.5KB of stack against an 8KB task -
    // measured with -Wstack-usage after the HTML-escaping buffers below were
    // added. Moving them to .bss removes that risk entirely rather than
    // trimming it.
    static char buf[720];
    const PortfolioTotals t = portfolioTotals();

    snprintf(buf, sizeof(buf),
        "<p>This device: <b>http://%s/</b> &nbsp;or&nbsp; http://%s/<br>"
        "Positions: %d (%d held) &nbsp;&middot;&nbsp; base %s</p>"
        "<button onclick=\"fetch('/refresh',{method:'POST'}).then(()=>location.reload())\">"
        "Refresh Now</button> "
        "<a href=\"/settings\"><button type=\"button\">Settings</button></a>",
        deviceHostname(), deviceAddress(), positionCount, heldCount(), settings.baseCurrency);
    sendChunk(buf);

    // Totals, so the web app is a dashboard too and not just an editor.
    if (t.valid) {
        char v[28], d[28], r[28], dp[16], rp[16];
        fmtMoney(v, sizeof(v), t.value);
        fmtMoney(d, sizeof(d), t.dayChangeAbs);
        fmtMoney(r, sizeof(r), t.totalReturnAbs);
        fmtPct(dp, sizeof(dp), t.dayChangePct,   2);
        fmtPct(rp, sizeof(rp), t.totalReturnPct, 2);
        snprintf(buf, sizeof(buf),
            "<hr><h2>Totals</h2><table>"
            "<tr><td>Value</td><td class=\"num\">%s %s</td></tr>"
            "<tr><td>Today</td><td class=\"num %s\">%s %s (%s)</td></tr>"
            "<tr><td>Total return</td><td class=\"num %s\">%s %s (%s)</td></tr>"
            "<tr><td>Cost basis</td><td class=\"num dim\">%.2f %s</td></tr></table>%s",
            v, settings.baseCurrency,
            t.dayChangePct >= 0 ? "good" : "bad", d, settings.baseCurrency, dp,
            t.totalReturnPct >= 0 ? "good" : "bad", r, settings.baseCurrency, rp,
            t.cost, settings.baseCurrency,
            t.anyUnconverted ? "<p class=\"bad\">Some positions are excluded from these "
                               "totals because no FX rate is available for their currency.</p>" : "");
        sendChunk(buf);
    }

    // Configuration problems first. Every one of these lets the device go on
    // displaying confident, wrong money - which is the failure mode worth
    // shouting about, not the one worth tucking into a diagnostics table.
    {
        // Its own buffer, sized from its parts rather than borrowed from the
        // page buffer whose remaining room depends on what came before it.
        static char missing[120], missingSafe[640], warn[960];
        const int nMissing = loanUnmatchedSymbols(missing, sizeof(missing));
        if (nMissing > 0) {
            // The unmatched names come straight from the loanSymbols setting,
            // which is free text - unlike a position's own symbol field, it is
            // never restricted to a safe character set.
            htmlEscape(missingSafe, sizeof(missingSafe), missing);
            snprintf(warn, sizeof(warn),
                "<hr><p class=\"bad\"><b>Check your setup:</b> the loan list names %d symbol(s) "
                "that are not in your positions &mdash; <b>%s</b>. They contribute nothing to the "
                "loan figures, which are therefore understated. Fix the spelling under Loan in "
                "<a href=\"/settings\">Settings</a>, or add the position.</p>", nMissing, missingSafe);
            sendChunk(warn);
        }

        for (int i = 0; i < positionCount; i++) {
            const Position& p = positions[i];
            if (!positionIsHeld(p) || !positionSuspectMove(p)) continue;
            char sus[16];
            fmtPct(sus, sizeof(sus), positionDayPct(p), 1);
            snprintf(warn, sizeof(warn),
                "<hr><p class=\"bad\"><b>Check your setup:</b> <b>%s</b> is showing a one-day "
                "move of %s. A broad fund does not do that; it is far more likely a split or "
                "another corporate action, in which case your quantity and average cost both "
                "need updating. Until then every figure involving this holding is wrong.</p>",
                p.symbol, sus);
            sendChunk(warn);
        }

        const uint32_t skew = portfolioQuoteSkew();
        if (skew > QUOTE_SKEW_MAX_S) {
            snprintf(warn, sizeof(warn),
                "<hr><p class=\"warn\"><b>Check your setup:</b> the prices being added together "
                "are up to %luh apart, so they are not all from the same session and the day "
                "change blends more than one day. Usually a venue holiday, or a symbol that has "
                "stopped trading.</p>", (unsigned long)(skew / 3600UL));
            sendChunk(warn);
        }

        const PortfolioTotals pt = portfolioTotals();
        if (pt.valid && pt.targetSum > 0 && (pt.targetSum < 90.0 || pt.targetSum > 110.0)) {
            snprintf(warn, sizeof(warn),
                "<hr><p class=\"warn\"><b>Check your setup:</b> your targets add up to %d%%, "
                "not 100%%. Drift is still computed by renormalising them against that sum, so the "
                "figures are self-consistent &mdash; but if you meant them as percentages of the "
                "whole sleeve, one is missing or mistyped.</p>", (int)lround(pt.targetSum));
            sendChunk(warn);
        }
    }

    {
        const LoanState L = loanCompute();
        if (L.valid) {
            char eq[28], pr[28], ow[28], in[28], va[28], ret[16], rate[16];
            fmtMoney(eq, sizeof(eq), L.equity);
            fmtMoney(pr, sizeof(pr), L.principal);
            fmtMoney(ow, sizeof(ow), L.owed);
            fmtMoney(in, sizeof(in), L.interest);
            fmtMoney(va, sizeof(va), L.value);
            fmtPct(ret,  sizeof(ret),  L.returnPct, 2);
            fmtPct(rate, sizeof(rate), L.ratePct,   2);
            snprintf(buf, sizeof(buf),
                "<hr><h2>Loan</h2><table>"
                "<tr><td>Drawn so far</td><td class=\"num\">%s %s <span class=\"dim\">(%d of %d)</span></td></tr>"
                "<tr><td>Interest accrued</td><td class=\"num warn\">%s %s</td></tr>"
                "<tr><td>Owed if repaid today</td><td class=\"num\">%s %s</td></tr>"
                "<tr><td>What it bought is worth</td><td class=\"num\">%s %s</td></tr>",
                pr, settings.baseCurrency, L.drawn, L.total,
                in, settings.baseCurrency,
                ow, settings.baseCurrency,
                va, settings.baseCurrency);
            sendChunk(buf);

            snprintf(buf, sizeof(buf),
                "<tr><td><b>Net equity</b></td><td class=\"num %s\"><b>%s %s</b></td></tr>"
                "<tr><td>Return vs cost of borrowing</td><td class=\"num %s\">%s vs %s</td></tr>"
                "</table>",
                L.equity >= 0 ? "good" : "bad", eq, settings.baseCurrency,
                L.returnPct >= L.ratePct ? "good" : "bad", ret, rate);
            sendChunk(buf);

            sendChunk("<p class=\"note\">Net equity is what would be left after repaying today. "
                      "The return is solved against the same drawdown schedule the interest is "
                      "charged on, so the two rates are directly comparable; it assumes each "
                      "tranche was invested when it was drawn.</p>");
        }
    }

    // Fetch diagnostics: "not fetched yet" only means something next to whether
    // the cycle is running at all.
    {
        const uint32_t nowMs = millis();
        const uint32_t endMs = marketsLastCycleEndMs();
        char since[40], pos[40];
        if (endMs == 0) snprintf(since, sizeof(since), "never");
        else            snprintf(since, sizeof(since), "%lus ago", (unsigned long)((nowMs - endMs) / 1000UL));
        if (marketsCycleIndex() < 0) snprintf(pos, sizeof(pos), "idle");
        else snprintf(pos, sizeof(pos), "fetching %d of %d", marketsCycleIndex() + 1, marketsCycleLength());

        snprintf(buf, sizeof(buf),
            "<hr><h2>Fetching</h2><table>"
            "<tr><td>Cycle</td><td>%s</td></tr>"
            "<tr><td>Cycles completed</td><td>%lu</td></tr>"
            "<tr><td>Last completed</td><td>%s</td></tr>"
            "<tr><td>Current interval</td><td>%lus</td></tr>"
            "<tr><td>FX rates</td><td>%d rate(s), base %s, dated %s</td></tr>"
            "<tr><td>History</td><td>%d day(s), %lu flash write(s) this boot</td></tr>"
            "<tr><td>Firmware update</td><td class=\"%s\">%s</td></tr>"
            "<tr><td>Free heap</td><td class=\"%s\">%lu bytes (low water %lu)</td></tr>"
            "<tr><td>Uptime</td><td>%lus</td></tr>"
            "<tr><td>Boots</td><td class=\"%s\">%lu</td></tr></table>",
            pos, (unsigned long)marketsCyclesCompleted(), since,
            (unsigned long)(marketsRefreshMs() / 1000UL),
            fxCount(), fxBaseCode(),
            portfolioNeedsFx() ? (fxDate()[0] ? fxDate() : "never")
                               : "not needed - single currency",
            historyCount(), (unsigned long)historyWrites(),
            OTA_PASSWORD[0] ? "" : "warn",
            OTA_PASSWORD[0] ? "over Wi-Fi, password set"
                            : "over Wi-Fi, NO PASSWORD - set OTA_PASSWORD in secrets.h",
            ESP.getMinFreeHeap() < 40000 ? "warn" : "",
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
            (unsigned long)(nowMs / 1000UL),
            settingsBootCount() > 20 ? "warn" : "", (unsigned long)settingsBootCount());
        sendChunk(buf);
    }

    if (t.valid && portfolioHasTargets()) {
        sendChunk("<hr><h2>Allocation</h2><table><tr><th>Fund</th>"
                  "<th class=\"num\">Of sleeve</th><th class=\"num\">Target</th>"
                  "<th class=\"num\">Drift</th><th class=\"num\">To target</th></tr>");
        for (int i = 0; i < positionCount; i++) {
            const Position& p = positions[i];
            if (!positionIsHeld(p) || !p.ok || p.target <= 0) continue;

            // Every column on this row shares one denominator: the targeted
            // sleeve. Showing share-of-everything beside a target defined over
            // the funds alone put three mutually contradictory numbers on one
            // line - 42.5 actual, 50 target, 0.0 drift.
            const double d   = positionDriftPct(p, t);
            const double mag = d < 0 ? -d : d;
            char act[16], tgt[16], drift[16], amt[28], label[64];
            fmtPct(act,   sizeof(act),   positionSleevePct(p, t), 1);
            fmtPct(tgt,   sizeof(tgt),   positionTargetPct(p, t), 1);
            fmtPct(drift, sizeof(drift), d, 1);
            stripPercent(drift);                        // points, not percent
            const double reb = positionRebalanceAmount(p, t);
            fmtMoney(amt, sizeof(amt), reb);
            htmlEscape(label, sizeof(label), p.label);   // a user-typed label, not a symbol

            static char row[400];
            snprintf(row, sizeof(row),
                "<tr><td>%s</td><td class=\"num\">%s</td>"
                "<td class=\"num dim\">%s</td>"
                "<td class=\"num %s\">%s</td>"
                "<td class=\"num %s\">%s%s %s</td></tr>",
                label, act + 1, tgt + 1,
                mag >= 2.0 ? "warn" : "good", drift,
                mag >= 0.5 ? "" : "dim", reb >= 0 ? "+" : "", amt, settings.baseCurrency);
            sendChunk(row);
        }
        sendChunk("</table>");

        // Untargeted holdings are named separately, against the whole book,
        // because that is the only denominator that means anything for them.
        {
            bool any = false;
            for (int i = 0; i < positionCount; i++) {
                const Position& p = positions[i];
                if (!positionIsHeld(p) || !p.ok || p.target > 0) continue;
                char w[16], label[64];
                static char row[240];
                fmtPct(w, sizeof(w), positionWeightPct(p, t), 1);
                htmlEscape(label, sizeof(label), p.label);
                if (!any) {
                    sendChunk("<p class=\"note\">Outside the targeted sleeve, as a share of the "
                              "whole book: ");
                    any = true;
                }
                snprintf(row, sizeof(row), "<b>%s</b> %s&nbsp; ", label, w + 1);
                sendChunk(row);
            }
            if (any) sendChunk("</p>");
        }

        sendChunk("<p class=\"note\">Drift is in percentage points within the targeted sleeve; "
                  "\"To target\" is what it would take to close it. Holdings with no target are "
                  "separate money and do not dilute the allocation. Set a target with a fifth "
                  "field: <b>SYMBOL,QTY,COST,LABEL,TARGET</b>.</p>");
    }

    if (positionCount > 0) {
        sendChunk("<hr><h2>Status</h2><table><tr><th>Symbol</th><th>Label</th>"
                  "<th class=\"num\">Qty</th><th class=\"num\">Price</th>"
                  "<th class=\"num\">Prev close</th><th class=\"num\">Day</th>"
                  "<th>State</th></tr>");
        for (int i = 0; i < positionCount; i++) {
            const Position& p = positions[i];
            char state[64];
            const char* cls = "bad";
            if (p.ok && !p.stale)     { snprintf(state, sizeof(state), "ok"); cls = "good"; }
            else if (p.ok && p.stale) { snprintf(state, sizeof(state), "stale (HTTP %d)", p.lastHttp); cls = "warn"; }
            else if (p.lastHttp == 0) { snprintf(state, sizeof(state), "not fetched yet"); cls = "warn"; }
            else                      { snprintf(state, sizeof(state), "no price - HTTP %d", p.lastHttp); }

            char label[64], currency[32];
            htmlEscape(label, sizeof(label), p.label);
            // The currency code comes from the quote provider, not from a field
            // this project restricts to a safe character set - see the note on
            // htmlEscape() above.
            htmlEscape(currency, sizeof(currency), p.currency);

            static char row[520];
            if (p.ok) {
                // Price and previous close side by side, because a portfolio
                // day change that looks wrong is almost always one position
                // whose two numbers come from different sessions - and that is
                // invisible until you can see both.
                const double dp = positionDayPct(p);
                char dayBuf[16];
                fmtPct(dayBuf, sizeof(dayBuf), dp, 2);
                const double mag = dp < 0 ? -dp : dp;
                snprintf(row, sizeof(row),
                    "<tr><td>%s</td><td class=\"dim\">%s</td><td class=\"num\">%s</td>"
                    "<td class=\"num\">%.4f %s</td>"
                    "<td class=\"num dim\">%.4f</td>"
                    "<td class=\"num %s\">%s</td>"
                    "<td class=\"%s\">%s</td></tr>",
                    p.symbol, label, positionIsHeld(p) ? "" : "watch",
                    p.price, currency, p.prevClose,
                    mag >= 5.0 ? "bad" : (dp >= 0 ? "good" : "warn"), dayBuf,
                    cls, state);
            } else snprintf(row, sizeof(row),
                "<tr><td>%s</td><td class=\"dim\">%s</td><td class=\"num\">%s</td>"
                "<td class=\"num\">&mdash;</td><td class=\"num\">&mdash;</td>"
                "<td class=\"num\">&mdash;</td><td class=\"%s\">%s</td></tr>",
                p.symbol, label, positionIsHeld(p) ? "" : "watch", cls, state);
            sendChunk(row);
            if (p.ok && positionIsHeld(p)) {
                static char qty[240];
                char retBuf[16];
                fmtPct(retBuf, sizeof(retBuf), positionReturnPct(p), 1);
                snprintf(qty, sizeof(qty),
                    "<tr><td class=\"dim\"></td><td class=\"dim\">%.10g units</td>"
                    "<td class=\"num dim\" colspan=\"5\">avg %.4f, %s total</td></tr>",
                    p.qty, p.avgCost, retBuf);
                sendChunk(qty);
            }
        }
        sendChunk("</table><p class=\"note\">A position with no price is almost always a symbol "
                  "Yahoo Finance does not serve on that venue. Check it resolves at "
                  "finance.yahoo.com/quote/&lt;SYMBOL&gt; and use the exact ticker shown there.</p>");
    }

    sendChunk(FORM);

    static char current[POSITION_TEXT_MAX];
    if (!portfolioSerialize(current, sizeof(current)))
        Serial.printf("[pf]    warning: position list did not fit the editor buffer\n");
    sendChunk(current);

    snprintf(buf, sizeof(buf), TAIL, POSITION_MAX, settings.baseCurrency);
    sendChunk(buf);
    endChunked();
}

static void onPositions() {
    if (!mayAccess()) return;
    if (!server.hasArg("h")) { server.send(400, "text/plain", "missing field"); return; }

    const String body = server.arg("h");
    static char result[700];

    if (portfolioSet(body.c_str())) {
        snprintf(result, sizeof(result),
            "<!DOCTYPE html><html><head><title>Pulsar</title>"
            "<meta http-equiv=\"refresh\" content=\"2; url=/\">"
            "<style>body{background:#06080F;color:#00CC55;font-family:monospace;"
            "max-width:520px;margin:40px auto;padding:20px}</style></head>"
            "<body>Saved %d position(s), %d held. Returning...</body></html>",
            positionCount, heldCount());
        refreshRequested = true;   // price the new positions straight away
        server.send(200, "text/html", result);
    } else {
        // A rejected line is quoted back in its own error - e.g. an invalid
        // symbol - so the message itself can carry whatever the submitter
        // typed. Escape it before it goes anywhere near the page.
        static char safeErr[400];
        htmlEscape(safeErr, sizeof(safeErr), portfolioLastError());
        snprintf(result, sizeof(result),
            "<!DOCTYPE html><html><head><title>Pulsar</title>"
            "<style>body{background:#06080F;color:#FF3344;font-family:monospace;"
            "max-width:520px;margin:40px auto;padding:20px}a{color:#33CCFF}</style></head>"
            "<body>Not saved: %s<br><br><a href=\"/\">Back</a></body></html>",
            safeErr);
        server.send(400, "text/html", result);
    }
}

static void onSettings() {
    if (!mayAccess()) return;
    static const char HEAD[] = R"HTML(<!DOCTYPE html>
<html><head><title>Pulsar settings</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{background:#06080F;color:#EEF2FF;font-family:monospace;max-width:560px;margin:32px auto;padding:20px}
h2{color:#33CCFF;margin-bottom:4px}p{color:#7A93AC;font-size:13px;line-height:1.5}
label{display:block;margin:14px 0;color:#7A93AC;font-size:13px}
input,select{width:100%;background:#0B1420;color:#EEF2FF;border:1px solid #1E3A52;border-radius:4px;
padding:8px;font-family:monospace;font-size:13px;box-sizing:border-box;margin-top:4px}
button{padding:10px 20px;margin-top:16px;background:#0D1B2A;border:1px solid #33CCFF;color:#33CCFF;
border-radius:4px;cursor:pointer;font-size:14px}
button:hover{filter:brightness(1.3)}
.danger{border-color:#FF3344;color:#FF3344}
.note{color:#3D5A73;font-size:12px}
.row{display:flex;gap:12px}.row>label{flex:1}
hr{border:0;border-top:1px solid #1E3A52;margin:24px 0}
a{color:#33CCFF}</style></head>
<body><h2>Settings</h2>
<p class="note">Changes apply immediately &mdash; no reflash, no reboot.</p>
<form method="POST" action="/settings">
)HTML";

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    sendChunk(HEAD);

    // Static for the same reason as onRoot()'s buf: this handler is never
    // reentrant, and its scratch buffers measured over 2KB of stack.
    static char buf[1200];

    snprintf(buf, sizeof(buf),
        "<label>Orientation<select name=\"rot\">"
        "<option value=\"0\"%s>Normal (USB left)</option>"
        "<option value=\"2\"%s>Flipped 180&deg;</option></select></label>"
        "<label>Brightness (0-255)<input name=\"bri\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>",
        settings.rotation == 0 ? " selected" : "",
        settings.rotation == 2 ? " selected" : "",
        settings.brightness);
    sendChunk(buf);

    static const char* VIEWS[VIEW_COUNT] = { "Positions grid", "Position detail",
                                             "Portfolio summary", "Allocation",
                                             "Loan" };
    int w = snprintf(buf, sizeof(buf), "<label>Default view on boot<select name=\"view\">");
    for (int i = 0; i < VIEW_COUNT; i++)
        w += snprintf(buf + w, sizeof(buf) - w, "<option value=\"%d\"%s>%s</option>",
                      i, i == settings.defaultView ? " selected" : "", VIEWS[i]);
    snprintf(buf + w, sizeof(buf) - w, "</select></label>");
    sendChunk(buf);

    snprintf(buf, sizeof(buf),
        "<label>Portfolio amounts<select name=\"amt\">"
        "<option value=\"2\"%s>Always on screen</option>"
        "<option value=\"1\"%s>Reveal for 6s on USER press</option>"
        "<option value=\"0\"%s>Never show amounts</option></select></label>"
        "<p class=\"note\">A percentage hides magnitude: 1%% of a small position and "
        "1%% of a large one read the same. \"Always\" shows today's move in money with "
        "the percentage beside it. Choose a hidden mode if the screen is somewhere "
        "other people walk past.</p><hr>",
        settings.amountMode == 2 ? " selected" : "",
        settings.amountMode == 1 ? " selected" : "",
        settings.amountMode == 0 ? " selected" : "");
    sendChunk(buf);

    {
        char from[8], to[8];
        snprintf(from, sizeof(from), "%02u:%02u", settings.nightStartMin / 60, settings.nightStartMin % 60);
        snprintf(to,   sizeof(to),   "%02u:%02u", settings.nightEndMin   / 60, settings.nightEndMin   % 60);
        snprintf(buf, sizeof(buf),
            "<hr><label>Night dimming<select name=\"nDim\">"
            "<option value=\"1\"%s>On</option>"
            "<option value=\"0\"%s>Off</option></select></label>"
            "<div class=\"row\">"
            "<label>From<input name=\"nStart\" type=\"time\" value=\"%s\"></label>"
            "<label>Until<input name=\"nEnd\" type=\"time\" value=\"%s\"></label>"
            "<label>Night brightness<input name=\"nBri\" type=\"number\" min=\"0\" max=\"255\" value=\"%u\"></label>"
            "</div>"
            "<p class=\"note\">Local wall-clock time, so it follows the timezone rule below "
            "through daylight saving. A window that runs past midnight is normal. Pressing "
            "BOOT still overrides the level by hand until the next change of schedule.</p><hr>",
            settings.nightDim ? " selected" : "",
            settings.nightDim ? "" : " selected",
            from, to, settings.nightBright);
        sendChunk(buf);
    }

    // Split across three writes: the loan block's literals alone come close to
    // buf, and a truncated settings form loses its closing tags and its fields.
    sendChunk("<hr><h2>Loan</h2>"
              "<p class=\"note\">Borrowed money invested in some of the positions. Leave the "
              "drawdown date empty to switch the loan view off entirely &mdash; it is left empty "
              "by default rather than guessing a date and reporting wrong money. Interest is "
              "treated as capitalised: it is added to the balance and compounds.</p>");

    snprintf(buf, sizeof(buf),
        "<label>First drawdown (YYYY-MM-DD)<input name=\"lStart\" value=\"%s\" "
        "placeholder=\"2025-09-01\" maxlength=\"10\"></label>"
        "<div class=\"row\">"
        "<label>Each drawdown<input name=\"lAmt\" value=\"%.2f\"></label>"
        "<label>Every (months)<input name=\"lIvl\" type=\"number\" min=\"1\" max=\"24\" value=\"%u\"></label>"
        "<label>Total drawdowns<input name=\"lN\" type=\"number\" min=\"0\" max=\"%d\" value=\"%u\"></label>"
        "<label>Rate %% / year<input name=\"lRate\" value=\"%.2f\"></label></div>",
        settings.loanStart, (double)settings.loanTranche, settings.loanIntervalM,
        LOAN_TRANCHE_MAX, settings.loanTranches, (double)settings.loanRatePct);
    sendChunk(buf);

    {
        // Unlike a position's symbol, this field is never restricted to a safe
        // character set - it only feeds a substring comparison - so it is
        // escaped before it goes into a value="" attribute it could otherwise
        // break out of.
        static char loanSyms[512];
        htmlEscape(loanSyms, sizeof(loanSyms), settings.loanSymbols);
        snprintf(buf, sizeof(buf),
            "<label>Positions the loan funded<input name=\"lSyms\" value=\"%s\"></label>"
            "<p class=\"note\">Comma-separated symbols. Anything left out is treated as your own "
            "money and stays out of the loan figures.</p><hr>",
            loanSyms);
        sendChunk(buf);
    }

    {
        // Neither field is character-restricted at save time (tz is a POSIX
        // rule string, fxurl only has to start with "https://"), so both are
        // escaped before landing in a value="" attribute.
        static char tz[256], fxurl[512];
        htmlEscape(tz,    sizeof(tz),    settings.tz);
        htmlEscape(fxurl, sizeof(fxurl), settings.fxUrl);
        snprintf(buf, sizeof(buf),
            "<div class=\"row\">"
            "<label>Base currency<input name=\"ccy\" value=\"%s\" maxlength=\"4\"></label>"
            "<label>Timezone (POSIX TZ)<input name=\"tz\" value=\"%s\"></label></div>"
            "<label>Exchange-rate URL<input name=\"fxurl\" value=\"%s\"></label>"
            "<p class=\"note\">Everything is converted into the base currency using ECB reference "
            "rates, so a mixed EUR/USD portfolio totals correctly. Rates move once a day.</p><hr>",
            settings.baseCurrency, tz, fxurl);
        sendChunk(buf);
    }

    snprintf(buf, sizeof(buf),
        "<p class=\"note\">Refresh intervals, in seconds. A cycle is one request per position, "
        "so very short intervals invite HTTP 429.</p>"
        "<div class=\"row\">"
        "<label>Market open<input name=\"rOpen\" type=\"number\" min=\"5\" max=\"3600\" value=\"%lu\"></label>"
        "<label>Pre/post<input name=\"rEdge\" type=\"number\" min=\"5\" max=\"3600\" value=\"%lu\"></label>"
        "<label>Closed<input name=\"rShut\" type=\"number\" min=\"5\" max=\"3600\" value=\"%lu\"></label></div>"
        "<button type=\"submit\">Save settings</button></form>",
        (unsigned long)(settings.refreshOpenMs   / 1000UL),
        (unsigned long)(settings.refreshEdgeMs   / 1000UL),
        (unsigned long)(settings.refreshClosedMs / 1000UL));
    sendChunk(buf);

    sendChunk("<form method=\"POST\" action=\"/settings\" "
              "onsubmit=\"return confirm('Reset all settings to firmware defaults?')\">"
              "<input type=\"hidden\" name=\"reset\" value=\"1\">"
              "<button type=\"submit\" class=\"danger\">Reset to defaults</button></form>");

    snprintf(buf, sizeof(buf),
        "<form method=\"POST\" action=\"/history\" "
        "onsubmit=\"return confirm('Erase recorded value history?')\">"
        "<input type=\"hidden\" name=\"clear\" value=\"1\">"
        "<button type=\"submit\" class=\"danger\">Clear history (%d days)</button></form>"
        "<p><a href=\"/\">&larr; Back to positions</a></p></body></html>",
        historyCount());
    sendChunk(buf);
    endChunked();
}

static void onSettingsSave() {
    if (!mayAccess()) return;
    if (server.hasArg("reset")) {
        settingsResetDefaults();
        displayApplyRotation();
        displayApplyScheduledBrightness();
        displayRefreshAll();
        server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"1; url=/settings\">"
            "<style>body{background:#06080F;color:#00CC55;font-family:monospace;margin:40px}</style>"
            "</head><body>Reset to defaults.</body></html>");
        return;
    }

    // Every field goes through settingsApply(), which validates one key at a
    // time and mutates the live `settings` struct as it goes. A form posts every
    // field in one request, so a bad value on, say, the 15th key used to leave
    // the first 14 already changed in RAM - live on screen and in every
    // fxConvert() from that moment - while the page reported "Not saved" and
    // NVS kept the old values. Worst case, a base-currency change would take
    // effect without the matching fxFetch() below ever running, so every
    // conversion after it silently used rates keyed to the wrong base. Taking a
    // full backup first and restoring it on any failure makes the whole POST
    // atomic: every key applies, or none of them visibly do.
    static const char* KEYS[] = { "rot","bri","view","amt","nDim","nBri","nStart","nEnd",
                                  "lStart","lAmt","lIvl","lN","lRate","lSyms","ccy","fxurl","tz","rOpen","rEdge","rShut" };
    static Settings backup;
    backup = settings;

    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (!server.hasArg(KEYS[i])) continue;
        if (!settingsApply(KEYS[i], server.arg(KEYS[i]).c_str())) {
            settings = backup;   // undo whatever earlier keys in this POST already changed
            static char safeErr[200], err[512];
            htmlEscape(safeErr, sizeof(safeErr), settingsLastError());
            snprintf(err, sizeof(err),
                "<!DOCTYPE html><html><head><title>Pulsar</title>"
                "<style>body{background:#06080F;color:#FF3344;font-family:monospace;margin:40px}"
                "a{color:#33CCFF}</style></head><body>Not saved: %s<br><br>"
                "<a href=\"/settings\">Back</a></body></html>", safeErr);
            server.send(400, "text/html", err);
            return;
        }
    }
    settingsSave();

    if (settings.rotation != backup.rotation) displayApplyRotation();
    // Cheap, and it resolves the case the old code got wrong: a brightness
    // change made during the night window must not light the panel back up.
    displayApplyScheduledBrightness();
    if (strncmp(backup.tz, settings.tz, TZ_MAX) != 0) timeBegin();
    // A new base currency invalidates the cached table's base, so refetch - but
    // not from here. fxFetch() is a blocking HTTPS call (up to ~16s on a slow or
    // unreachable endpoint) and this handler runs on the same task as loop(), so
    // calling it inline would freeze buttons, the display and the watchdog feed
    // for the duration. Flag it instead and let loop()'s already-gated fxFetch()
    // path pick it up, exactly as a manual refresh defers to marketsStartCycle().
    if (strncmp(backup.baseCurrency, settings.baseCurrency, CCY_MAX) != 0) fxFetchRequested = true;
    displayRefreshAll();

    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"1; url=/settings\">"
        "<style>body{background:#06080F;color:#00CC55;font-family:monospace;margin:40px}</style>"
        "</head><body>Saved.</body></html>");
}

static void onHistoryClear() {
    if (!mayAccess()) return;
    historyClear();
    displayRefreshAll();
    server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"1; url=/settings\">"
        "<style>body{background:#06080F;color:#00CC55;font-family:monospace;margin:40px}</style>"
        "</head><body>History cleared.</body></html>");
}

static void onRefresh() {
    if (!mayAccess()) return;
    refreshRequested = true;
    server.send(200, "application/json", R"({"ok":true})");
}

// ── Wi-Fi ─────────────────────────────────────────────────────────────────────
// Connecting no longer fetches data as a side effect: loop() owns scheduling,
// so a reconnect cannot smuggle six blocking HTTPS requests into a retry.
void wifiBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // board is USB powered; trade idle mA for latency
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

// Only used behind the boot splash, which already says "Connecting...".
bool wifiWaitConnected(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!wifiConnected() && millis() - start < timeoutMs) delay(100);
    return wifiConnected();
}

// Nudges a reconnect at most every WIFI_RETRY_MS and returns immediately, so an
// outage no longer freezes the display for 20 s out of every 30.
void wifiMaintain(uint32_t now) {
    static uint32_t lastRetry = 0;
    if (wifiConnected()) { lastRetry = now; return; }
    if (now - lastRetry < WIFI_RETRY_MS) return;
    lastRetry = now;
    WiFi.reconnect();
}

// ── Time ──────────────────────────────────────────────────────────────────────
void timeBegin() {
    configTzTime(settings.tz, NTP_SERVER_1, NTP_SERVER_2);
}

bool localNow(struct tm* out) {
    time_t nowSec = time(nullptr);
    if (nowSec <= 1700000000) return false;
    localtime_r(&nowSec, out);
    return true;
}

// ── Watchdog ──────────────────────────────────────────────────────────────────
static bool s_wdtOn = false;

void watchdogBegin() {
    if (esp_task_wdt_init(WDT_TIMEOUT_S, true) != ESP_OK) {   // true: panic, so it reboots
        Serial.printf("[wdt]   could not start\n");
        return;
    }
    esp_task_wdt_add(nullptr);       // watch the Arduino loop task
    s_wdtOn = true;
    Serial.printf("[wdt]   armed at %lus\n", (unsigned long)WDT_TIMEOUT_S);
}

void watchdogFeed() { if (s_wdtOn) esp_task_wdt_reset(); }

// ── OTA ───────────────────────────────────────────────────────────────────────
static bool s_otaActive = false;
static int  s_otaPct    = -1;
static bool s_otaBegun  = false;   // guards ArduinoOTA.begin() against running twice

bool otaInProgress() { return s_otaActive; }

void otaBegin() {
    if (s_otaBegun) return;
    if (!wifiConnected()) { Serial.printf("[ota]   no Wi-Fi; OTA not started\n"); return; }
    s_otaBegun = true;

    ArduinoOTA.setHostname(MDNS_HOST);
    if (OTA_PASSWORD[0]) {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    } else {
        Serial.printf("[ota]   WARNING: no OTA_PASSWORD in secrets.h - anyone on this "
                      "network can reflash this device\n");
    }

    ArduinoOTA.onStart([]() {
        // An OTA write legitimately outruns the watchdog, and today's history
        // point is about to be lost to the reboot unless it is flushed now.
        historyFlush();
        if (s_wdtOn) { esp_task_wdt_delete(nullptr); s_wdtOn = false; }
        s_otaActive = true;
        s_otaPct    = -1;
        drawOtaScreen(-1, "receiving", false);
        drawOtaScreen(0,  "receiving", false);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        const int pct = total ? (int)(((uint64_t)done * 100ULL) / total) : 0;
        if (pct == s_otaPct) return;        // redraw only when the number changes
        s_otaPct = pct;
        drawOtaScreen(pct, "receiving", false);
    });
    ArduinoOTA.onEnd([]() {
        drawOtaScreen(100, "rebooting", false);
    });
    ArduinoOTA.onError([](ota_error_t err) {
        s_otaActive = false;
        // onStart() disarmed the watchdog for the write; a failed update must
        // not leave it that way, or the device runs the rest of its uptime with
        // watchdogFeed() a silent no-op - exactly the hung-task case the
        // watchdog exists to catch.
        if (!s_wdtOn && esp_task_wdt_add(nullptr) == ESP_OK) s_wdtOn = true;
        char note[40];
        snprintf(note, sizeof(note), "error %d", (int)err);
        drawOtaScreen(-1, note, true);
        delay(3000);
        displayRefreshAll();
    });

    ArduinoOTA.begin();
    Serial.printf("[ota]   ready: pio run -t upload --upload-port %s\n", MDNS_HOST ".local");
}

void otaHandle() { ArduinoOTA.handle(); }

// ── Address ───────────────────────────────────────────────────────────────────
static char s_addr[48] = "not connected";
static char s_host[32] = MDNS_HOST ".local";

const char* deviceAddress()  { return s_addr; }
const char* deviceHostname() { return s_host; }

// mDNS, the address string and OTA all need Wi-Fi up to start, and setup()
// only tries once, after a bounded 20s wait behind the boot splash. A device
// that powers on before its own router is ready - both coming up together is
// an ordinary event, not a rare one - would otherwise lose all three for the
// rest of its uptime: wifiMaintain() reconnects it seconds later, but nothing
// else was ever called again. This runs from setup() if Wi-Fi is already up,
// and again from loop() on the next Wi-Fi-up transition if it wasn't - the IP
// string refreshes every time in case DHCP handed out a new one, while mDNS
// and OTA are one-time work, guarded further down.
void networkOnWifiUp() {
    if (!wifiConnected()) return;
    snprintf(s_addr, sizeof(s_addr), "%s", WiFi.localIP().toString().c_str());
    Serial.printf("[net]   http://%s/\n", s_addr);

    static bool mdnsBegun = false;
    if (!mdnsBegun) {
        mdnsBegun = true;
        if (MDNS.begin(MDNS_HOST)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("[net]   http://%s/\n", s_host);
        } else {
            Serial.printf("[net]   mDNS failed; use the IP\n");
        }
    }

    otaBegin();   // idempotent: a no-op once it has already started
}

void setupServer() {
    networkOnWifiUp();

    server.on("/",        HTTP_GET,  onRoot);
    server.on("/refresh",  HTTP_POST, onRefresh);
    server.on("/positions", HTTP_POST, onPositions);
    server.on("/settings", HTTP_GET,  onSettings);
    server.on("/settings", HTTP_POST, onSettingsSave);
    server.on("/history",  HTTP_POST, onHistoryClear);
    server.begin();
}
