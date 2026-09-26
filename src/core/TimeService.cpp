#include "TimeService.h"

struct TzOption { const char *name; const char *posix; };

// POSIX TZ strings (they include daylight-saving rules)
static const TzOption TZS[] = {
  {"Pacific (US)",      "PST8PDT,M3.2.0,M11.1.0"},
  {"Mountain (US)",     "MST7MDT,M3.2.0,M11.1.0"},
  {"Arizona (no DST)",  "MST7"},
  {"Central (US)",      "CST6CDT,M3.2.0,M11.1.0"},
  {"Eastern (US)",      "EST5EDT,M3.2.0,M11.1.0"},
  {"Alaska",            "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Hawaii",            "HST10"},
  {"UTC",               "UTC0"},
  {"London",            "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Central Europe",    "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"India",             "IST-5:30"},
  {"Japan",             "JST-9"},
  {"Sydney",            "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
static const int TZ_COUNT = sizeof(TZS) / sizeof(TZS[0]);

int timeZoneCount() { return TZ_COUNT; }

const char *timeZoneName(int index) {
  if (index < 0 || index >= TZ_COUNT) return "";
  return TZS[index].name;
}

static uint8_t gZone = 3;             // Central (US), until timeSetZone is called

const char *timeZonePosix() { return TZS[gZone].posix; }

void timeSetZone(uint8_t index) {
  if (index >= TZ_COUNT) index = 3;   // Central (US)
  gZone = index;
  configTzTime(TZS[index].posix, "pool.ntp.org", "time.nist.gov");
}

bool timeNow(struct tm &t) {
  return getLocalTime(&t, 0);
}

// Howard Hinnant's days_from_civil algorithm.
long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}
