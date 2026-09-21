#include "history.h"
#include <Preferences.h>
#include <string.h>

static DayPoint    s_points[HISTORY_MAX] = {};
static int         s_count = 0;
static Preferences prefs;
static const char* NVS_NS = "pulsarhist";

int historyCount() { return s_count; }

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

// Portfolio value as of the closest recorded day at or before `days` back from
// `today`. A device that was off for a stretch has ordinary gaps in the
// series, so this is deliberately not an exact-day lookup - but a gap wider
// than the window itself would silently answer "7-day return" with a point
// that is actually three weeks old, which is worse than not answering, so
// that case is declined rather than mislabelled.
bool historyValueDaysAgo(const struct tm& today, int days, double* value, double* cost,
                          int maxGapDays) {
    if (!value || !cost || days <= 0 || s_count == 0) return false;
    if (maxGapDays < 0) maxGapDays = days;
    const uint16_t todayIdx = historyDayIndex(today);
    if (todayIdx < (uint16_t)days) return false;   // the series can't reach that far back
    const uint16_t target = (uint16_t)(todayIdx - days);

    int best = -1;
    for (int i = 0; i < s_count; i++) {
        if (s_points[i].day > target) break;
        best = i;
    }
    if (best < 0) return false;
    if ((uint16_t)(target - s_points[best].day) > (uint16_t)maxGapDays) return false;

    *value = s_points[best].value;
    *cost  = s_points[best].cost;
    return true;
}

// One point from January 1st, kept forever - independent of the ~4-month ring
// buffer above, and the reason a year-to-date figure is not limited to the
// January-April window that buffer alone would allow. ~10 bytes and one more
// NVS key bought a full year's reach rather than tripling HISTORY_MAX, which
// would have meant tripling this device's already-tight shared NVS partition
// (see history.h) for the same result.
static DayPoint s_yearAnchor      = {};
static bool     s_yearAnchorValid = false;

bool historyYearStart(const struct tm& today, double* value, double* cost) {
    if (!value || !cost || !s_yearAnchorValid) return false;
    const uint16_t todayIdx = historyDayIndex(today);
    const uint16_t jan1     = (uint16_t)(todayIdx - today.tm_yday);
    if (s_yearAnchor.day != jan1) return false;   // the anchor on file is a different year
    *value = s_yearAnchor.value;
    *cost  = s_yearAnchor.cost;
    return true;
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
    s_yearAnchorValid = prefs.getBool("ancOk", false);
    if (s_yearAnchorValid) prefs.getBytes("anchor", &s_yearAnchor, sizeof(DayPoint));
    prefs.end();
    Serial.printf("[hist]  %d day(s) of history%s\n", s_count,
                  s_yearAnchorValid ? ", year anchor set" : "");
}

void historyClear() {
    s_count = 0;
    memset(s_points, 0, sizeof(s_points));
    s_yearAnchor      = {};
    s_yearAnchorValid = false;
    s_writes++;
    s_lastSaveMs = millis();
    s_dirty      = false;
    prefs.begin(NVS_NS, false);
    prefs.putInt("n", s_count);
    prefs.putBytes("pts", s_points, sizeof(DayPoint) * s_count);
    prefs.putBool("ancOk", false);
    prefs.end();
}

// Captured the moment a January 1st is actually recorded, not derived after
// the fact from the ring buffer above - so it survives long after that
// buffer's ~4-month reach would otherwise let the same information lapse.
// tm_yday == 0 is exactly "today is January 1st"; repeat calls on the same
// January 1st simply refresh the same day's figure, the same as the ring
// buffer's own same-day overwrite just above.
static void maybeCaptureYearAnchor(const struct tm& t, uint16_t day, double value, double cost) {
    if (t.tm_yday != 0) return;
    s_yearAnchor.day   = day;
    s_yearAnchor.value = (float)value;
    s_yearAnchor.cost  = (float)cost;
    s_yearAnchorValid  = true;
    prefs.begin(NVS_NS, false);
    prefs.putBytes("anchor", &s_yearAnchor, sizeof(DayPoint));
    prefs.putBool("ancOk", true);
    prefs.end();
}

bool historyRecord(const struct tm& t, double value, double cost) {
    const uint16_t day = historyDayIndex(t);

    // Same day: overwrite, so the stored figure is the latest one that day
    // rather than whatever happened to be first after a reboot.
    if (s_count > 0 && s_points[s_count - 1].day == day) {
        s_points[s_count - 1].value = (float)value;
        s_points[s_count - 1].cost  = (float)cost;
        saveThrottled();
        maybeCaptureYearAnchor(t, day, value, cost);
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
    maybeCaptureYearAnchor(t, day, value, cost);
    Serial.printf("[hist]  day %u recorded: value=%.2f cost=%.2f (%d stored)\n",
                  day, value, cost, s_count);
    return true;
}
