#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;
extern bool      refreshRequested;

// ── Wi-Fi ─────────────────────────────────────────────────────────────────────
void wifiBegin();                             // non-blocking: starts associating
bool wifiConnected();
bool wifiWaitConnected(uint32_t timeoutMs);   // blocking — boot splash only
void wifiMaintain(uint32_t now);              // non-blocking reconnect, call each loop

// ── Time ──────────────────────────────────────────────────────────────────────
// SNTP + POSIX TZ, so CET/CEST switches without anyone editing a constant.
void timeBegin();
bool timeSynced();
bool localNow(struct tm* out);                // false until the first sync lands

// ── Web ───────────────────────────────────────────────────────────────────────
void setupServer();
