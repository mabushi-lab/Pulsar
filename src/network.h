#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;
extern bool      refreshRequested;
extern bool      fxFetchRequested;   // set by the settings page; fxFetch() itself is blocking

// ── Wi-Fi ─────────────────────────────────────────────────────────────────────
void wifiBegin();                             // non-blocking: starts associating
bool wifiConnected();
bool wifiWaitConnected(uint32_t timeoutMs);   // blocking — boot splash only
void wifiMaintain(uint32_t now);              // non-blocking reconnect, call each loop

// ── Time ──────────────────────────────────────────────────────────────────────
// SNTP + POSIX TZ, so CET/CEST switches without anyone editing a constant.
void timeBegin();
bool localNow(struct tm* out);                // false until the first sync lands

// ── OTA ───────────────────────────────────────────────────────────────────────
// Firmware over Wi-Fi, so a change no longer means finding the USB-C cable.
// Push with:  pio run -t upload --upload-port pulsar.local
// Set OTA_PASSWORD in secrets.h: without one, anyone on the network can replace
// the firmware on this device, and the device will say so on its own web page.
void otaBegin();
void otaHandle();        // call every loop; returns at once when nothing is happening
bool otaInProgress();

// ── Watchdog ──────────────────────────────────────────────────────────────────
// The device makes blocking HTTPS calls, and a TLS handshake against an
// unresponsive host can hang past any timeout the client sets. Without this the
// result is a frozen screen that still looks powered; with it, the device
// reboots and the boot counter on the web page shows it happened.
void watchdogBegin();
void watchdogFeed();

// ── Web ───────────────────────────────────────────────────────────────────────
void setupServer();

// Starts mDNS, records the current address, and starts OTA - all of which need
// Wi-Fi up. Called once from setupServer() and again on every later Wi-Fi-up
// transition, since the one at boot can miss a router that is still coming up.
void networkOnWifiUp();

// "pulsar.local" plus the dotted IP, for the boot screen and the web page.
const char* deviceAddress();
const char* deviceHostname();
