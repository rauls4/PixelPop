#pragma once
// Reminder list helpers with no hardware in them (so they can be tested on a PC):
//   * reading the list the Shortcuts app sends (one reminder per line: "Title | 2026-09-21 15:30 | Groceries | !"),
//   * putting the reminders in a useful order and picking the ones to show,
//   * the text for the band across the top ("OVERDUE 2D", "TODAY 3P", "WED 9A" ...).

#include <stddef.h>
#include "../calendar/CalData.h"           // ts_t and the date helpers

static const int REM_MAX = 30;

struct RemItem {
  char  title[CAL_TITLE_LEN] = "";
  char  list[24] = "";                      // name of the Reminders list it belongs to ("" = not given)
  bool  hasDue = false, hasTime = false, high = false;
  ts_t  due = 0;                            // local wall-clock time read as UTC seconds
};

// Lines of "Title" followed by any of these, in any order, each after a bar: the due date
// (YYYY-MM-DD or YYYY-MM-DD HH:MM), a "!" (or "high" / "urgent") for high priority, and the
// name of the list, e.g. "Buy milk | 2026-09-21 15:30 | Groceries | !". A leading ! in the
// title also means high priority. Returns how many items were read.
int remParse(const char *text, RemItem *out, int maxItems);

enum RemState { REM_OVERDUE, REM_TODAY, REM_SOON, REM_LATER, REM_NONE };
// nowWall = the current local time, read as UTC seconds.
RemState remState(const RemItem &r, ts_t nowWall);
RemState remHeader(const RemItem &r, ts_t nowWall, bool h24, char *out, size_t cap);

// The small line under the title: the due date and the list, e.g. "MON SEP 21 3:30P - GROCERIES"
// (just the date, just the list, or "" when it has neither).
void remDetail(const RemItem &r, bool h24, char *out, size_t cap);

// Picks what to show: overdue first, then by due date, then reminders with no date. windowDays:
// 0 = today and overdue only, 3, 7, or -1 for everything. Fills order[] with indexes, returns the count.
int remOrder(const RemItem *items, int n, ts_t nowWall, int windowDays, bool showUndated, int *order, int maxShown);
