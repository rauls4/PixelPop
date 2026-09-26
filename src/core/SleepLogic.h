#pragma once
// The rule behind the sleep schedule, kept free of any hardware so it can be tested on a computer.
//
// A schedule is "turn the display off at 22:00, back on at 07:00" on some days of the week.
// The days chosen are the days the night STARTS on: Friday ticked with 22:00 -> 07:00 means
// Friday 22:00 until Saturday 07:00. An "on" time earlier than the "off" time is an overnight
// window; equal times mean nothing (a zero-length window).

#include <stdint.h>

struct SleepSched {
  bool     enabled = false;
  uint16_t off = 22 * 60;      // minute of the day the display turns off
  uint16_t wake = 7 * 60;      // minute of the day it turns back on
  uint8_t  days = 0x7F;        // bit n = the night starting on weekday n (0 = Sunday, like tm_wday)
};

// wday: 0 = Sunday ... 6 = Saturday.  minute: 0..1439
inline bool sleepInWindow(const SleepSched &s, int wday, int minute) {
  if (!s.enabled || s.days == 0) return false;
  const int dur = ((int)s.wake - (int)s.off + 1440) % 1440;
  if (dur == 0) return false;
  const int now = wday * 1440 + minute;
  for (int d = 0; d < 7; d++) {
    if (!((s.days >> d) & 1)) continue;
    const int delta = (now - (d * 1440 + (int)s.off) + 10080) % 10080;
    if (delta < dur) return true;
  }
  return false;
}

inline bool sleepScheduled(const SleepSched *list, int n, int wday, int minute) {
  for (int i = 0; i < n; i++) if (sleepInWindow(list[i], wday, minute)) return true;
  return false;
}
