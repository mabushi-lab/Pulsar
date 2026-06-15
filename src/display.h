#pragma once

void displayInit();
void displaySetBrightness(bool full);

// ── Boot sequence ─────────────────────────────────────────────────────────────
void animSplash();       // typewriter title + "connecting" before WiFi
void animRevealMain();   // staggered panel reveal after WiFi connects

// ── Runtime animations ────────────────────────────────────────────────────────
void triggerPanelFlash(int idx);  // call before drawAllMarkets() on each fetch
void animTick();                  // call every loop iteration

// ── Brightness ───────────────────────────────────────────────────────────────
void displayCycleBrightness();   // cycles full → dim → off → full

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
void drawDividers();
void drawAll();
