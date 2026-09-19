#include "history.h"
#include <Preferences.h>
#include <string.h>

static DayPoint    s_points[HISTORY_MAX] = {};
static int         s_count = 0;
static Preferences prefs;
static const char* NVS_NS = "pulsarhist";

int             historyCount() { return s_count; }
const DayPoint* historyData()  { return s_points; }

// Days since 2020-01-01. tm_year is years since 1900, tm_yday is 0-based.
static uint16_t historyDayIndex(const struct tm& t) {
    const int year = t.tm_year + 1900;
    long days = 0;
    for (int y = 2020; y < year; y++) {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }
    days += t.tm_yday;
    if (days < 0) days = 0;
    if (days > 65535) days = 65535;
    return (uint16_t)days;
}

static uint32_t s_writes      = 0;
static uint32_t s_lastSaveMs  = 0;
static bool     s_dirty       = false;

uint32_t historyWrites() { return s_writes; }

static void save() {
    s_writes++;
    s_lastSaveMs = millis();
    s_dirty      = false;
    prefs.begin(NVS_NS, false);
    prefs.putInt("n", s_count);
    prefs.putBytes("pts", s_points, sizeof(DayPoint) * s_count);
    prefs.end();
}

// A same-day update: keep it in RAM, and only reach for flash once the throttle
// has elapsed. The first write of a boot is never throttled, so a device that
// crashes early still has something stored.
static void saveThrottled() {
    if (s_lastSaveMs != 0 && (millis() - s_lastSaveMs) < HISTORY_SAVE_MIN_MS) {
        s_dirty = true;
        return;
    }
    save();
}

void historyFlush() { if (s_dirty) save(); }

void historyBegin() {
    prefs.begin(NVS_NS, true);
    s_count = prefs.getInt("n", 0);
    if (s_count > 0 && s_count <= HISTORY_MAX) prefs.getBytes("pts", s_points, sizeof(DayPoint) * s_count);
    else s_count = 0;
    prefs.end();
    Serial.printf("[hist]  %d day(s) of history\n", s_count);
}

void historyClear() {
    s_count = 0;
    memset(s_points, 0, sizeof(s_points));
    save();
}

bool historyRecord(const struct tm& t, double value, double cost) {
    const uint16_t day = historyDayIndex(t);

    // Same day: overwrite, so the stored figure is the latest one that day
    // rather than whatever happened to be first after a reboot.
    if (s_count > 0 && s_points[s_count - 1].day == day) {
        s_points[s_count - 1].value = (float)value;
        s_points[s_count - 1].cost  = (float)cost;
        saveThrottled();
        return false;
    }

    // A clock that jumps backwards (bad NTP, then corrected) must not corrupt
    // the series into a non-monotonic mess.
    if (s_count > 0 && day < s_points[s_count - 1].day) return false;

    if (s_count == HISTORY_MAX) {
        memmove(&s_points[0], &s_points[1], sizeof(DayPoint) * (HISTORY_MAX - 1));
        s_count--;
    }
    s_points[s_count].day   = day;
    s_points[s_count].value = (float)value;
    s_points[s_count].cost  = (float)cost;
    s_count++;
    save();
    Serial.printf("[hist]  day %u recorded: value=%.2f cost=%.2f (%d stored)\n",
                  day, value, cost, s_count);
    return true;
}
