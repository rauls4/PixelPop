#pragma once
// Shared time-of-day service (NTP + time zone). Used by the clock, countdown
// and chime modules.

#include <Arduino.h>
#include <time.h>

int         timeZoneCount();
const char *timeZoneName(int index);
void        timeSetZone(uint8_t index);       // starts/re-configures NTP with this zone
bool        timeNow(struct tm &t);            // true once the clock has synced
const char *timeZonePosix();                  // POSIX rule of the zone in use, e.g. "PST8PDT,M3.2.0,M11.1.0"

// Day number of a calendar date (days since 1970-01-01). Subtracting two of
// these gives whole calendar days between dates, independent of time zones/DST.
long        daysFromCivil(int year, int month, int day);
