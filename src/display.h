#pragma once
#include <stdint.h>   // uint8_t below; do not rely on a transitive include

struct TradingSession;

void displayInit();
void displayApplyRotation();
void displayRefreshAll();                 // repaint the current view, no animation

// ── Boot sequence ─────────────────────────────────────────────────────────────
void animSplash();
void animShowAddress();                   // where to reach the web app
void animRevealMain();

// ── Runtime animations ────────────────────────────────────────────────────────
void triggerPanelFlash(int slot);         // slot within the current page
void animTick();

void displayCycleBrightness();            // full (255) → dim (70) → off (0)

// ── Night dimming ─────────────────────────────────────────────────────────────
// Acts only on a day↔night transition, so a manual brightness press stays in
// force until the schedule next has something to say. Call once a second.
void    displayBrightnessTick();
void    displayApplyScheduledBrightness();   // force now (settings just changed)
// ── OTA ───────────────────────────────────────────────────────────────────────
// A firmware push blanks the screen for ~20 s; without this the device looks
// dead at exactly the moment you most want to know it is alive.
void drawOtaScreen(int pct, const char* note, bool error);

// ── Views ─────────────────────────────────────────────────────────────────────
// BOOT long-press cycles: positions → detail → portfolio → loan.
// Allocation is not a stop on the BOOT cycle. It and Loan are two faces of the
// same thing - the borrowed, targeted sleeve of funds - so USER flips between
// them and BOOT treats the pair as one destination. Five screens, four stops.
enum DisplayView { VIEW_POSITIONS, VIEW_DETAIL, VIEW_PORTFOLIO, VIEW_ALLOC, VIEW_LOAN };
const int VIEW_COUNT = 5;    // selectable as a boot view; not the cycle length
void        displaySetView(DisplayView v);
DisplayView displayView();
void        displayNextView();

// USER short-press means "next" within a view: next page, next position,
// reveal amounts, or flip between Loan and Allocation.
void displayUserAction();

int  displayDetailIndex();
int  displaySlotForPosition(int posIndex);// -1 when not on the visible page

// ── Draws ─────────────────────────────────────────────────────────────────────
void drawHeader();
void drawPositionPanel(int slot);
void drawDetail();
void drawPortfolio();
void drawAllocation();
void drawLoan();
void drawFooter();
void drawDividers();

void displaySetRefreshCountdown(int secs);
