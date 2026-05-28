#pragma once

void displayInit();
void displaySetBrightness(bool full);

// ── Boot sequence ─────────────────────────────────────────────────────────────
void animSplash();       // typewriter title + "connecting" before WiFi
void animRevealMain();   // staggered panel reveal after WiFi connects

// ── Runtime animations ────────────────────────────────────────────────────────
void triggerPanelFlash(int idx);  // call before drawAllMarkets() on each fetch
void animTick();                  // call every loop iteration

// ── Draw primitives ───────────────────────────────────────────────────────────
void drawHeader();
void drawMarketPanel(int idx);
void drawAllMarkets();
void drawProgress();
void drawDividers();
void drawAll();
