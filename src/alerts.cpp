#include "alerts.h"
#include "config.h"
#include "settings.h"
#include "portfolio.h"
#include "loan.h"
#include "network.h"
#include "data.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

// A message never carries a quote, a symbol's value, or account data - only
// which condition changed and a fund's own label - but a label is free text
// (see portfolioLabelValid() in portfolio.cpp) and can contain a '"' or a
// backslash, either of which would corrupt the JSON body if substituted in
// raw. Escaped the same way network.cpp's htmlEscape() exists for HTML.
static void jsonEscape(char* out, size_t outSize, const char* in) {
    if (!out || !outSize) return;
    size_t o = 0;
    for (const char* c = in ? in : ""; *c && o + 7 < outSize; c++) {
        const unsigned char u = (unsigned char)*c;
        if (*c == '"' || *c == '\\') { out[o++] = '\\'; out[o++] = *c; }
        else if (u < 0x20) { o += (size_t)snprintf(out + o, outSize - o, "\\u%04x", u); }
        else out[o++] = *c;
    }
    out[o] = '\0';
}

// Same posture as fetchQuote()/fxFetch() in network.cpp: no certificate
// pinning, because a routine rotation on the receiving end would otherwise
// turn into alerts that silently stop arriving. What crosses this connection
// is a short status line about the user's own portfolio, already visible in
// full to anyone with LAN access to the (unauthenticated by default) web
// dashboard - a successful intercept learns nothing that page does not
// already show.
static bool postWebhook(const char* text) {
    if (!settings.webhookUrl[0] || !wifiConnected()) return false;

    char safe[300];
    jsonEscape(safe, sizeof(safe), text);
    char body[340];
    snprintf(body, sizeof(body), "{\"text\":\"%s\"}", safe);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, settings.webhookUrl);
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    http.addHeader("Content-Type", "application/json");

    const int code = http.POST((uint8_t*)body, strlen(body));
    Serial.printf("[alert] HTTP %d  %s\n", code, text);
    http.end();
    return code >= 200 && code < 300;
}

bool alertsSendTest() {
    return postWebhook("Pulsar: this is a test alert. If you can read this, the webhook is set up correctly.");
}

// One latch per condition, so a webhook fires exactly once at the transition
// rather than once per fetch cycle for as long as the condition holds.
static bool s_suspectActive        = false;
static bool s_loanUnmatchedActive  = false;
static bool s_driftActive          = false;

// Each returns true if it posted, so alertsCheck() can stop at the first one.
// A latch is only ever written by the branch that actually posted for it -
// never assigned unconditionally - so a condition that was not even looked at
// this cycle (because an earlier one already spent this cycle's one post)
// keeps whatever it was, and gets a fair detection-vs-latch comparison on the
// next call instead of being silently marked "already reported".

static bool checkSuspect() {
    const char* suspectSymbol = nullptr;
    for (int i = 0; i < positionCount; i++) {
        if (positionIsHeld(positions[i]) && positionSuspectMove(positions[i])) {
            suspectSymbol = positions[i].symbol;
            break;
        }
    }
    const bool suspect = suspectSymbol != nullptr;
    if (suspect == s_suspectActive) return false;

    char msg[128];
    if (suspect) snprintf(msg, sizeof(msg),
                          "Pulsar: %s moved implausibly (>%.0f%% in a day) - check for a split",
                          suspectSymbol, SPLIT_SUSPECT_PCT);
    else         snprintf(msg, sizeof(msg), "Pulsar: the suspect price move has cleared.");
    postWebhook(msg);
    s_suspectActive = suspect;
    return true;
}

static bool checkLoanUnmatched() {
    const bool active = loanUnmatchedSymbols(nullptr, 0) > 0;
    if (active == s_loanUnmatchedActive) return false;

    postWebhook(active ? "Pulsar: the loan list names a symbol that matches no position."
                        : "Pulsar: the loan symbol list now matches your positions.");
    s_loanUnmatchedActive = active;
    return true;
}

// The worst-off targeted fund crossing the same threshold the Portfolio and
// Allocation screens already flag in orange. A little hysteresis around the
// line keeps a fund sitting right on the boundary from firing on and off
// every cycle. Holds its current state rather than clearing it when there is
// simply nothing priced yet to judge - a transient fetch failure must not
// fabricate a "back within target" message that has nothing to do with the
// drift actually having resolved; "no targets configured" is the one case
// that is a real, definitive not-drifting.
static bool checkDrift() {
    bool driftBad = s_driftActive;
    const Position* worstPos = nullptr;
    double worstDrift = 0.0;
    const PortfolioTotals t = portfolioTotals();
    if (!t.valid || !portfolioHasTargets()) {
        driftBad = false;
    } else {
        const int wi = worstDriftIndex(t);
        if (wi >= 0) {
            worstDrift = positionDriftPct(positions[wi], t);
            worstPos = &positions[wi];
            const double mag = worstDrift < 0 ? -worstDrift : worstDrift;
            if      (mag >= DRIFT_WARN_PCT)        driftBad = true;
            else if (mag <  DRIFT_WARN_PCT * 0.75) driftBad = false;
        }
    }
    if (driftBad == s_driftActive) return false;

    if (driftBad && worstPos) {
        char msg[112], driftBuf[16];
        fmtPct(driftBuf, sizeof(driftBuf), worstDrift < 0 ? -worstDrift : worstDrift, 1);
        stripPercent(driftBuf);   // points, not percent - same distinction the screens draw
        snprintf(msg, sizeof(msg), "Pulsar: %s is %s by %s points from target.",
                 worstPos->label, worstDrift >= 0 ? "heavy" : "light", driftBuf);
        postWebhook(msg);
    } else {
        postWebhook("Pulsar: drift is back within target.");
    }
    s_driftActive = driftBad;
    return true;
}

void alertsCheck() {
    // No URL, or no link to send it over: leave every latch exactly as it is.
    // Updating a latch without a successful post would mark a condition
    // "already reported" when it never went anywhere, and it would stay silent
    // once the link came back.
    if (!settings.webhookUrl[0] || !wifiConnected()) return;

    // At most one webhook per call. Each is a blocking HTTPS POST - up to 8s
    // connect plus 8s read, the same budget fetchQuote() and fxFetch() already
    // spend per request - and three conditions can plausibly change in the
    // same cycle (a loan typo and a drift past target, say). Sending all three
    // back to back would run past WDT_TIMEOUT_S (45s) and panic-reboot, which
    // resets every latch to false and fires the same alerts again on the next
    // boot: a reboot loop caused by the alerting feature itself. One completed
    // cycle is 15s-15min away, so the next condition is never far behind.
    if (checkSuspect()) return;
    if (checkLoanUnmatched()) return;
    checkDrift();
}
