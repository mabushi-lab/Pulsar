#pragma once

// ── Webhook alerts ────────────────────────────────────────────────────────────
// Optional. Off unless settings.webhookUrl is set (see Settings in settings.h).
// Fires a Slack-compatible {"text": "..."} POST when one of the conditions the
// device already detects but only shows passively on screen starts or clears:
// a probable stock split, a loan-list symbol that matches no position, or a
// fund drifted past DRIFT_WARN_PCT. Each fires once at the transition, not on
// every fetch cycle, so a single unresolved condition does not turn into a
// webhook ping every fifteen seconds.

// Evaluates the current state against what was last reported and posts for
// any condition that started or cleared since. Cheap when nothing changed -
// the only network call is the POST itself, and only on a transition - so it
// is safe to call once per completed fetch cycle.
void alertsCheck();

// Posts a fixed message right now, regardless of state, so setting up a
// webhook URL does not mean waiting for a real warning to find out it works.
// Returns false if there is no URL configured or the request did not succeed.
bool alertsSendTest();
