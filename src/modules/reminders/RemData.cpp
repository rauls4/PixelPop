#include "RemData.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "../ticker/TickerData.h"          // tickerCleanText

static long floorDiv(ts_t a, long b) { ts_t q = a / b; if ((a % b) != 0 && ((a < 0) != (b < 0))) q--; return (long)q; }

static bool digits(const char *s, int n) { for (int i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false; return true; }
static int num(const char *s, int n) { int v = 0; while (n--) v = v * 10 + (*s++ - '0'); return v; }

static void trim(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
  size_t lead = 0;
  while (s[lead] == ' ' || s[lead] == '\t') lead++;
  if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
}

// "2026-09-21", "2026-09-21 15:30", "2026-09-21T15:30:00"
static bool parseDue(const char *s, RemItem &r) {
  if (strlen(s) < 10 || !digits(s, 4) || s[4] != '-' || !digits(s + 5, 2) || s[7] != '-' || !digits(s + 8, 2)) return false;
  const int y = num(s, 4), mo = num(s + 5, 2), d = num(s + 8, 2);
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  int h = 0, mi = 0;
  bool time = false;
  if ((s[10] == ' ' || s[10] == 'T') && digits(s + 11, 2) && s[13] == ':' && digits(s + 14, 2)) {
    h = num(s + 11, 2); mi = num(s + 14, 2);
    time = h < 24 && mi < 60;
    if (!time) { h = mi = 0; }
  }
  r.hasDue = true;
  r.hasTime = time;
  r.due = calEpoch(y, mo, d, h, mi, 0);
  return true;
}

static bool isHigh(const char *s) {
  if (s[0] == '!') { for (const char *c = s; *c; c++) if (*c != '!') return false; return true; }     // "!", "!!", "!!!"
  return !strcasecmp(s, "high") || !strcasecmp(s, "urgent") || !strcasecmp(s, "yes");
}

int remParse(const char *text, RemItem *out, int maxItems) {
  int n = 0;
  const char *p = text;
  while (*p && n < maxItems) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char line[200];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    p = end ? end + 1 : p + len;
    if (!end && len == 0) break;

    // split on | (or a tab)
    char *f[6] = {line, nullptr, nullptr, nullptr, nullptr, nullptr};
    int nf = 1;
    for (char *c = line; *c && nf < 6; c++) if (*c == '|' || *c == '\t') { *c = 0; f[nf++] = c + 1; }
    trim(f[0]);
    if (!f[0][0]) continue;
    RemItem r;
    char *t = f[0];
    while (*t == '!') { r.high = true; t++; }
    while (*t == ' ') t++;
    char clean[CAL_TITLE_LEN + 16];
    tickerCleanText(t, clean, sizeof(clean));
    trim(clean);
    if (!clean[0]) continue;
    strncpy(r.title, clean, CAL_TITLE_LEN - 1);
    r.title[CAL_TITLE_LEN - 1] = 0;
    // the other fields can come in any order: a date, a priority mark, or the list name
    for (int i = 1; i < nf; i++) {
      trim(f[i]);
      if (!f[i][0]) continue;
      if (!r.hasDue && parseDue(f[i], r)) continue;
      if (isHigh(f[i])) { r.high = true; continue; }
      if (!r.list[0]) {
        char lc[40];
        tickerCleanText(f[i], lc, sizeof(lc));
        trim(lc);
        strncpy(r.list, lc, sizeof(r.list) - 1);
        r.list[sizeof(r.list) - 1] = 0;
      }
    }
    out[n++] = r;
  }
  return n;
}

RemState remState(const RemItem &r, ts_t now) {
  if (!r.hasDue) return REM_NONE;
  const long dayDiff = floorDiv(r.due, 86400) - floorDiv(now, 86400);
  if (dayDiff < 0) return REM_OVERDUE;
  if (dayDiff == 0) return (r.hasTime && r.due < now) ? REM_OVERDUE : REM_TODAY;
  return dayDiff <= 6 ? REM_SOON : REM_LATER;
}

static const char *const DAY_NAMES[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MON_NAMES[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void fmtTime(char *out, size_t cap, const CalTm &t, bool h24) {
  if (h24) { snprintf(out, cap, "%d:%02d", t.hour, t.min); return; }
  int h = t.hour % 12; if (!h) h = 12;
  if (t.min) snprintf(out, cap, "%d:%02d%c", h, t.min, t.hour < 12 ? 'A' : 'P');
  else       snprintf(out, cap, "%d%c", h, t.hour < 12 ? 'A' : 'P');
}

RemState remHeader(const RemItem &r, ts_t now, bool h24, char *out, size_t cap) {
  const RemState st = remState(r, now);
  if (!r.hasDue) { snprintf(out, cap, "REMINDER"); return st; }
  const long dayDiff = floorDiv(r.due, 86400) - floorDiv(now, 86400);
  CalTm t; calBreak(r.due, t);
  char tm[12] = "";
  if (r.hasTime) fmtTime(tm, sizeof(tm), t, h24);
  if (st == REM_OVERDUE) {
    if (dayDiff < 0) snprintf(out, cap, "OVERDUE %ldD", -dayDiff);
    else snprintf(out, cap, "OVERDUE %s", tm);
    return st;
  }
  char day[12];
  if (dayDiff == 0) snprintf(day, sizeof(day), "TODAY");
  else if (dayDiff == 1) snprintf(day, sizeof(day), "TOMORROW");
  else if (dayDiff < 7) snprintf(day, sizeof(day), "%s", DAY_NAMES[t.wday]);
  else snprintf(day, sizeof(day), "%s %d", MON_NAMES[(t.mon - 1) % 12], t.day);
  if (tm[0]) snprintf(out, cap, "%s %s", day, tm);
  else snprintf(out, cap, "%s", day);
  return st;
}

void remDetail(const RemItem &r, bool h24, char *out, size_t cap) {
  out[0] = 0;
  char date[28] = "";
  if (r.hasDue) {
    CalTm t; calBreak(r.due, t);
    char tm[12] = "";
    if (r.hasTime) fmtTime(tm, sizeof(tm), t, h24);
    snprintf(date, sizeof(date), "%s %s %d%s%s", DAY_NAMES[t.wday], MON_NAMES[(t.mon - 1) % 12], t.day, tm[0] ? " " : "", tm);
  }
  char list[sizeof(r.list)];
  size_t i = 0;
  for (; r.list[i] && i < sizeof(list) - 1; i++) list[i] = (r.list[i] >= 'a' && r.list[i] <= 'z') ? r.list[i] - 32 : r.list[i];
  list[i] = 0;
  if (date[0] && list[0]) snprintf(out, cap, "%s - %s", date, list);
  else if (date[0]) snprintf(out, cap, "%s", date);
  else snprintf(out, cap, "%s", list);
}

int remOrder(const RemItem *items, int n, ts_t now, int windowDays, bool showUndated, int *order, int maxShown) {
  int idx[REM_MAX], m = 0;
  for (int i = 0; i < n && i < REM_MAX; i++) {
    const RemItem &r = items[i];
    if (!r.hasDue) { if (showUndated) idx[m++] = i; continue; }
    const long dayDiff = floorDiv(r.due, 86400) - floorDiv(now, 86400);
    if (windowDays >= 0 && dayDiff > windowDays) continue;
    idx[m++] = i;
  }
  // stable insertion sort: dated ones by due time (overdue naturally first), undated last
  for (int i = 1; i < m; i++) {
    const int v = idx[i];
    int j = i - 1;
    auto before = [&](int a, int b) {                // does a go before b?
      const RemItem &x = items[a], &y = items[b];
      if (x.hasDue != y.hasDue) return x.hasDue;
      if (!x.hasDue) return false;
      return x.due < y.due;
    };
    while (j >= 0 && before(v, idx[j])) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = v;
  }
  if (m > maxShown) m = maxShown;
  for (int i = 0; i < m; i++) order[i] = idx[i];
  return m;
}
