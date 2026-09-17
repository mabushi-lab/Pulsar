#pragma once

void displayInit();

// ── Boot sequence ─────────────────────────────────────────────────────────────
void animSplash();       // typewriter title + "connecting" before WiFi
void animRevealMain();   // staggered panel reveal after WiFi connects

// ── Runtime animations ────────────────────────────────────────────────────────
void triggerPanelFlash(int idx);  // call before drawAllMarkets() on each fetch
void animTick();                  // call every loop iteration

// ── Brightness ───────────────────────────────────────────────────────────────
void displayCycleBrightness();   // cycles full (255) → dim (70) → off (0) → full

// ── View mode ─────────────────────────────────────────────────────────────────
// Toggle between 6-market grid and a single large Silver panel.
// Controlled by holding BOOT button (>=600 ms); short tap still cycles brightness.
void displaySetSilverView(bool silver);
bool displayIsSilverView();
void drawSilverOnly();

// ── Pomodoro ──────────────────────────────────────────────────────────────────
// state: 0 = idle (not started), 1 = running, 2 = paused
void drawPomodoro(int secsLeft, int totalSecs, bool isWork, int state, int sessions);
void animPomAlert(bool wasWork); // brief flash when a phase completes

// ── Draw primitives ───────────────────────────────────────────────────────────
void drawHeader();
void drawMarketPanel(int idx);
void drawAllMarkets();
void drawProgress();
const struct TradingSession& displayedSession();
void drawDividers();

// ── Refresh countdown ─────────────────────────────────────────────────────────
// Call every second with seconds until the next market fetch.
// Shown as a small right-aligned indicator in the footer label row.
void displaySetRefreshCountdown(int secs);
