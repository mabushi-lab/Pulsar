#pragma once

// All drawing goes through these functions — the LGFX/lcd instance is private
// to display.cpp and never exposed directly.

void displayInit();
void displaySetBrightness(bool full);
void drawSplash(const char* msg);
void drawHeader();
void drawMarketPanel(int idx);
void drawAllMarkets();
void drawProgress();
void drawDividers();
void drawAll();
