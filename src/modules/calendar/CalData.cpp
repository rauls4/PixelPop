#include "CalData.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "../ticker/TickerData.h"          // tickerCleanText: UTF-8 to plain ASCII

// ======================= dates =======================
static long floorDiv(ts_t a, long b) { ts_t q = a / b; if ((a % b) != 0 && ((a < 0) != (b < 0))) q--; return (long)q; }

long calDaysFromCivil(int y, int m, int d) {
  y -= (m <= 2);
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

void calCivilFromDays(long z, int &y, int &m, int &d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const long doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yoe + era * 400 + (m <= 2));
}

int calDaysInMonth(int y, int m) {
  static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[(m - 1) % 12];
}

int calWeekday(long days) { return (int)(((days % 7) + 7 + 4) % 7); }   // 1970-01-01 was a Thursday

ts_t calEpoch(int y, int m, int d, int h, int mi, int s) {
  return (ts_t)calDaysFromCivil(y, m, d) * 86400 + h * 3600 + mi * 60 + s;
}

void calBreak(ts_t t, CalTm &o) {
  long days = floorDiv(t, 86400);
  long sec = (long)(t - (ts_t)days * 86400);
  calCivilFromDays(days, o.year, o.mon, o.day);
  o.hour = (int)(sec / 3600); o.min = (int)(sec % 3600 / 60); o.sec = (int)(sec % 60);
  o.wday = calWeekday(days);
}

// ======================= time zones =======================
// Reads a POSIX offset like "8", "-5:30", "+3" at *pp; returns seconds (as written, west positive).
static bool readOffset(const char *&p, int &secs) {
  int sign = 1;
  if (*p == '+') p++;
  else if (*p == '-') { sign = -1; p++; }
  if (*p < '0' || *p > '9') return false;
  int h = 0, m = 0, s = 0;
  while (*p >= '0' && *p <= '9') h = h * 10 + (*p++ - '0');
  if (*p == ':') { p++; while (*p >= '0' && *p <= '9') m = m * 10 + (*p++ - '0'); }
  if (*p == ':') { p++; while (*p >= '0' && *p <= '9') s = s * 10 + (*p++ - '0'); }
  secs = sign * (h * 3600 + m * 60 + s);
  return true;
}

static bool skipName(const char *&p) {
  const char *s = p;
  if (*p == '<') { while (*p && *p != '>') p++; if (*p == '>') p++; }
  else while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
  return p > s;
}

static bool readRule(const char *&p, int &m, int &w, int &d, int &t) {
  if (*p != 'M') return false;
  p++;
  m = atoi(p); while (*p >= '0' && *p <= '9') p++;
  if (*p != '.') return false;
  p++;
  w = atoi(p); while (*p >= '0' && *p <= '9') p++;
  if (*p != '.') return false;
  p++;
  d = atoi(p); while (*p >= '0' && *p <= '9') p++;
  t = 7200;
  if (*p == '/') { p++; if (!readOffset(p, t)) return false; }
  return m >= 1 && m <= 12 && w >= 1 && w <= 5 && d >= 0 && d <= 6;
}

bool calZoneParse(const char *posix, CalZone &z) {
  z = CalZone();
  const char *p = posix;
  if (!p || !skipName(p)) return false;
  int off;
  if (!readOffset(p, off)) return false;
  z.stdOff = -off;
  z.dstOff = z.stdOff;
  if (!*p) return true;
  if (!skipName(p)) return false;
  z.hasDst = true;
  z.dstOff = z.stdOff + 3600;
  if (*p && *p != ',') { if (!readOffset(p, off)) return false; z.dstOff = -off; }
  if (*p == ',') {
    p++;
    if (!readRule(p, z.sm, z.sw, z.sd, z.st)) return false;
    if (*p != ',') return false;
    p++;
    if (!readRule(p, z.em, z.ew, z.ed, z.et)) return false;
  } else {                                           // no rules given: the US ones
    z.sm = 3; z.sw = 2; z.sd = 0; z.st = 7200; z.em = 11; z.ew = 1; z.ed = 0; z.et = 7200;
  }
  return true;
}

// The moment (UTC) a rule like "second Sunday of March at 2:00" fires in year y.
static ts_t ruleUtc(int y, int m, int w, int d, int t, int offBefore) {
  const int dow1 = calWeekday(calDaysFromCivil(y, m, 1));
  int day = 1 + (d - dow1 + 7) % 7 + (w - 1) * 7;
  while (day > calDaysInMonth(y, m)) day -= 7;
  return calEpoch(y, m, day, 0, 0, 0) + t - offBefore;
}

int calZoneOffsetUtc(const CalZone &z, ts_t utc) {
  if (!z.hasDst) return z.stdOff;
  CalTm tm;
  calBreak(utc + z.stdOff, tm);
  const ts_t s = ruleUtc(tm.year, z.sm, z.sw, z.sd, z.st, z.stdOff);
  const ts_t e = ruleUtc(tm.year, z.em, z.ew, z.ed, z.et, z.dstOff);
  const bool dst = (s < e) ? (utc >= s && utc < e) : (utc >= s || utc < e);
  return dst ? z.dstOff : z.stdOff;
}

ts_t calZoneLocalToUtc(const CalZone &z, ts_t wall) {
  int off = calZoneOffsetUtc(z, wall - z.stdOff);
  ts_t utc = wall - off;
  int off2 = calZoneOffsetUtc(z, utc);
  if (off2 != off) utc = wall - off2;
  return utc;
}

struct IanaZone { const char *id; const char *posix; };
#define US_PAC   "PST8PDT,M3.2.0,M11.1.0"
#define US_MTN   "MST7MDT,M3.2.0,M11.1.0"
#define US_CEN   "CST6CDT,M3.2.0,M11.1.0"
#define US_EAST  "EST5EDT,M3.2.0,M11.1.0"
#define EU_CET   "CET-1CEST,M3.5.0,M10.5.0/3"
#define EU_EET   "EET-2EEST,M3.5.0/3,M10.5.0/4"
static const IanaZone IANA[] = {
  {"America/Los_Angeles", US_PAC}, {"US/Pacific", US_PAC}, {"America/Vancouver", US_PAC}, {"America/Tijuana", US_PAC},
  {"America/Denver", US_MTN}, {"US/Mountain", US_MTN}, {"America/Edmonton", US_MTN}, {"America/Boise", US_MTN},
  {"America/Phoenix", "MST7"}, {"US/Arizona", "MST7"},
  {"America/Chicago", US_CEN}, {"US/Central", US_CEN}, {"America/Winnipeg", US_CEN}, {"America/Indiana/Knox", US_CEN},
  {"America/Mexico_City", "CST6"}, {"America/Regina", "CST6"},
  {"America/New_York", US_EAST}, {"US/Eastern", US_EAST}, {"America/Toronto", US_EAST}, {"America/Detroit", US_EAST},
  {"America/Indiana/Indianapolis", US_EAST}, {"America/Kentucky/Louisville", US_EAST},
  {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"US/Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Pacific/Honolulu", "HST10"}, {"US/Hawaii", "HST10"},
  {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"}, {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
  {"America/Puerto_Rico", "AST4"}, {"America/Bogota", "<-05>5"}, {"America/Lima", "<-05>5"},
  {"America/Sao_Paulo", "<-03>3"}, {"America/Argentina/Buenos_Aires", "<-03>3"}, {"America/Buenos_Aires", "<-03>3"},
  {"UTC", "UTC0"}, {"GMT", "UTC0"}, {"Etc/UTC", "UTC0"}, {"Etc/GMT", "UTC0"}, {"Z", "UTC0"},
  {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
  {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Europe/Paris", EU_CET}, {"Europe/Berlin", EU_CET}, {"Europe/Madrid", EU_CET}, {"Europe/Rome", EU_CET},
  {"Europe/Amsterdam", EU_CET}, {"Europe/Brussels", EU_CET}, {"Europe/Vienna", EU_CET}, {"Europe/Zurich", EU_CET},
  {"Europe/Stockholm", EU_CET}, {"Europe/Oslo", EU_CET}, {"Europe/Copenhagen", EU_CET}, {"Europe/Prague", EU_CET},
  {"Europe/Warsaw", EU_CET}, {"Europe/Budapest", EU_CET},
  {"Europe/Athens", EU_EET}, {"Europe/Helsinki", EU_EET}, {"Europe/Kiev", EU_EET}, {"Europe/Kyiv", EU_EET},
  {"Europe/Bucharest", EU_EET}, {"Europe/Sofia", EU_EET},
  {"Europe/Moscow", "MSK-3"}, {"Europe/Istanbul", "<+03>-3"},
  {"Asia/Dubai", "<+04>-4"}, {"Asia/Karachi", "PKT-5"}, {"Asia/Kolkata", "IST-5:30"}, {"Asia/Calcutta", "IST-5:30"},
  {"Asia/Dhaka", "<+06>-6"}, {"Asia/Bangkok", "<+07>-7"}, {"Asia/Jakarta", "WIB-7"}, {"Asia/Shanghai", "CST-8"},
  {"Asia/Hong_Kong", "HKT-8"}, {"Asia/Singapore", "<+08>-8"}, {"Asia/Taipei", "CST-8"}, {"Asia/Manila", "PST-8"},
  {"Asia/Tokyo", "JST-9"}, {"Asia/Seoul", "KST-9"},
  {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Australia/Brisbane", "AEST-10"}, {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {"Australia/Perth", "AWST-8"}, {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"Africa/Johannesburg", "SAST-2"}, {"Africa/Lagos", "WAT-1"}, {"Africa/Nairobi", "EAT-3"},
};

const char *calIanaToPosix(const char *tzid) {
  if (!tzid || !*tzid) return nullptr;
  for (size_t i = 0; i < sizeof(IANA) / sizeof(IANA[0]); i++)
    if (strcasecmp(tzid, IANA[i].id) == 0) return IANA[i].posix;
  return nullptr;
}

// ======================= iCalendar reader =======================
static uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
  return h;
}

static bool digitsN(const char *s, int n) {
  for (int i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}
static int num(const char *s, int n) { int v = 0; while (n--) v = v * 10 + (*s++ - '0'); return v; }

// "P1DT2H30M", "PT45M", "P1W", "-PT15M"
static bool parseDuration(const char *s, long &out) {
  long sign = 1, total = 0, cur = 0;
  bool time = false, any = false;
  if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
  if (*s != 'P') return false;
  for (s++; *s; s++) {
    if (*s >= '0' && *s <= '9') { cur = cur * 10 + (*s - '0'); continue; }
    switch (*s) {
      case 'T': time = true; break;
      case 'W': total += cur * 7 * 86400; any = true; break;
      case 'D': total += cur * 86400; any = true; break;
      case 'H': total += cur * 3600; any = true; break;
      case 'M': if (time) total += cur * 60; any = true; break;
      case 'S': total += cur; any = true; break;
      default: return false;
    }
    cur = 0;
  }
  out = sign * total;
  return any;
}

// Finds TZID=... in a property's parameter text.
static bool findTzid(const char *params, char *out, size_t cap) {
  for (const char *p = params; *p; p++) {
    if ((p == params || p[-1] == ';') && strncasecmp(p, "TZID=", 5) == 0) {
      p += 5;
      if (*p == '"') p++;
      size_t n = 0;
      while (*p && *p != ';' && *p != '"' && n + 1 < cap) out[n++] = *p++;
      out[n] = 0;
      return n > 0;
    }
  }
  return false;
}

// Text values: \n \, \; \\ escapes, then UTF-8 to ASCII.
static void cleanTitle(const char *val, char *out, size_t cap) {
  char tmp[160];
  size_t n = 0;
  for (const char *p = val; *p && n + 1 < sizeof(tmp); p++) {
    if (*p == '\\' && p[1]) {
      p++;
      tmp[n++] = (*p == 'n' || *p == 'N') ? ' ' : *p;
    } else tmp[n++] = *p;
  }
  tmp[n] = 0;
  tickerCleanText(tmp, out, cap);
  // trim
  size_t len = strlen(out);
  while (len && out[len - 1] == ' ') out[--len] = 0;
  size_t lead = 0;
  while (out[lead] == ' ') lead++;
  if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
}

void IcsParser::begin(ts_t winLo, ts_t winHi, const char *localPosix) {
  _lo = winLo; _hi = winHi;
  if (!localPosix || !calZoneParse(localPosix, _board)) calZoneParse("UTC0", _board);
  _sawCal = _inEvent = _inAlarm = false;
  _seen = 0; _physLen = _lineLen = 0; _n = 0; _nOvr = _ovrNext = 0;
  _p = Pend();
}

void IcsParser::feed(char c) {
  if (c == '\r') return;
  if (c == '\n') { physLine(); _physLen = 0; return; }
  if (_physLen < LINE_LEN - 1) _phys[_physLen++] = c;
}

void IcsParser::finish() {
  if (_physLen) { physLine(); _physLen = 0; }
  if (_lineLen) { _line[_lineLen] = 0; processLine(_line); _lineLen = 0; }
}

// A physical line: a continuation (starts with space/tab) joins the previous one.
void IcsParser::physLine() {
  _phys[_physLen] = 0;
  if (_physLen > 0 && (_phys[0] == ' ' || _phys[0] == '\t')) {
    for (int i = 1; i < _physLen && _lineLen < LINE_LEN - 1; i++) _line[_lineLen++] = _phys[i];
    return;
  }
  if (_lineLen) { _line[_lineLen] = 0; processLine(_line); }
  memcpy(_line, _phys, _physLen);
  _lineLen = _physLen;
}

bool IcsParser::parseDt(const char *params, const char *val, Dt &dt) {
  const size_t L = strlen(val);
  if (L < 8 || !digitsN(val, 8)) return false;
  const int y = num(val, 4), mo = num(val + 4, 2), d = num(val + 6, 2);
  int h = 0, mi = 0, s = 0;
  const bool hasTime = (L >= 15 && val[8] == 'T' && digitsN(val + 9, 6));
  if (hasTime) { h = num(val + 9, 2); mi = num(val + 11, 2); s = num(val + 13, 2); }
  const bool isUtc = hasTime && L >= 16 && val[15] == 'Z';
  dt.dateOnly = !hasTime;
  dt.wall = calEpoch(y, mo, d, h, mi, s);
  if (isUtc) { calZoneParse("UTC0", dt.zone); }
  else if (dt.dateOnly) dt.zone = _board;
  else {
    char tzid[48];
    const char *posix = findTzid(params, tzid, sizeof(tzid)) ? calIanaToPosix(tzid) : nullptr;
    if (!posix || !calZoneParse(posix, dt.zone)) dt.zone = _board;      // unknown zone: use the board's own
  }
  dt.utc = calZoneLocalToUtc(dt.zone, dt.wall);
  dt.ok = true;
  return true;
}

void IcsParser::parseRule(char *val) {
  Rule &r = _p.rr;
  r = Rule();
  char *tok = val;
  while (tok && *tok) {
    char *next = strchr(tok, ';');
    if (next) *next++ = 0;
    char *eq = strchr(tok, '=');
    if (eq) {
      *eq++ = 0;
      const char *k = tok;
      if (!strcasecmp(k, "FREQ")) {
        r.freq = !strcasecmp(eq, "DAILY") ? 1 : !strcasecmp(eq, "WEEKLY") ? 2 : !strcasecmp(eq, "MONTHLY") ? 3 : !strcasecmp(eq, "YEARLY") ? 4 : 0;
      } else if (!strcasecmp(k, "INTERVAL")) { r.interval = atoi(eq); if (r.interval < 1) r.interval = 1; }
      else if (!strcasecmp(k, "COUNT")) r.count = atoi(eq);
      else if (!strcasecmp(k, "UNTIL")) {
        Dt u;
        if (parseDt("", eq, u)) { r.hasUntil = true; r.until = u.utc + (u.dateOnly ? 86399 : 0); }
      } else if (!strcasecmp(k, "WKST")) {
        static const char *const D[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
        for (int i = 0; i < 7; i++) if (!strncasecmp(eq, D[i], 2)) r.wkst = i;
      } else if (!strcasecmp(k, "BYMONTH")) {
        for (char *c = eq; *c;) { int m = atoi(c); if (m >= 1 && m <= 12) r.byMonth |= (1u << m); c = strchr(c, ','); if (!c) break; c++; }
      } else if (!strcasecmp(k, "BYMONTHDAY")) {
        for (char *c = eq; *c && r.nByMonthDay < MAX_BYMD;) { int v = atoi(c); if (v) r.byMonthDay[r.nByMonthDay++] = (int8_t)v; c = strchr(c, ','); if (!c) break; c++; }
      } else if (!strcasecmp(k, "BYDAY")) {
        static const char *const D[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
        for (char *c = eq; *c && r.nByDay < MAX_BYDAY;) {
          int sign = 1, ord = 0;
          if (*c == '-') { sign = -1; c++; } else if (*c == '+') c++;
          while (*c >= '0' && *c <= '9') ord = ord * 10 + (*c++ - '0');
          for (int i = 0; i < 7; i++) if (!strncasecmp(c, D[i], 2)) { r.byDay[r.nByDay].ord = (int8_t)(sign * ord); r.byDay[r.nByDay].wd = (int8_t)i; r.nByDay++; break; }
          c = strchr(c, ','); if (!c) break; c++;
        }
      }
    }
    tok = next;
  }
}

void IcsParser::processLine(char *ln) {
  char *colon = nullptr, *semi = nullptr;
  bool quoted = false;
  for (char *c = ln; *c; c++) {
    if (*c == '"') quoted = !quoted;
    else if (!quoted) {
      if (*c == ';' && !semi) semi = c;
      if (*c == ':') { colon = c; break; }
    }
  }
  if (!colon) return;
  char *val = colon + 1;
  const char *params = "";
  if (semi && semi < colon) { params = semi + 1; *semi = 0; }
  *colon = 0;
  for (char *c = ln; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
  const char *name = ln;

  if (!strcmp(name, "BEGIN")) {
    if (!strcasecmp(val, "VCALENDAR")) _sawCal = true;
    else if (!strcasecmp(val, "VEVENT")) { _p = Pend(); _inEvent = true; _inAlarm = false; }
    else if (!strcasecmp(val, "VALARM")) _inAlarm = true;
    return;
  }
  if (!strcmp(name, "END")) {
    if (!strcasecmp(val, "VEVENT")) { if (_inEvent) finishEvent(); _inEvent = false; }
    else if (!strcasecmp(val, "VALARM")) _inAlarm = false;
    return;
  }
  if (!_inEvent || _inAlarm) return;

  if (!strcmp(name, "SUMMARY")) cleanTitle(val, _p.title, sizeof(_p.title));
  else if (!strcmp(name, "DTSTART")) parseDt(params, val, _p.start);
  else if (!strcmp(name, "DTEND")) { if (parseDt(params, val, _p.end)) _p.haveEnd = true; }
  else if (!strcmp(name, "DURATION")) { long d; if (parseDuration(val, d)) { _p.durSec = d; _p.haveDur = true; } }
  else if (!strcmp(name, "UID")) _p.uid = fnv1a(val);
  else if (!strcmp(name, "STATUS")) { if (!strcasecmp(val, "CANCELLED")) _p.cancelled = true; }
  else if (!strcmp(name, "RRULE")) parseRule(val);
  else if (!strcmp(name, "RECURRENCE-ID")) { Dt d; if (parseDt(params, val, d)) { _p.haveRec = true; _p.recId = d.utc; } }
  else if (!strcmp(name, "EXDATE")) {
    for (char *c = val; c && *c;) {
      char *next = strchr(c, ',');
      if (next) *next++ = 0;
      Dt d;
      if (parseDt(params, c, d) && _p.nEx < MAX_EX) _p.exd[_p.nEx++] = d.utc;
      c = next;
    }
  }
}

// ---------- repeating events ----------
bool IcsParser::overridden(uint32_t uid, ts_t startUtc) const {
  for (int i = 0; i < _nOvr; i++) if (_ovr[i].uid == uid && _ovr[i].recId == startUtc) return true;
  return false;
}

void IcsParser::dropInstance(uint32_t uid, ts_t orig) {
  for (int i = 0; i < _n;) {
    if (_ev[i].uid == uid && _ev[i].orig == orig) { for (int j = i; j + 1 < _n; j++) _ev[j] = _ev[j + 1]; _n--; }
    else i++;
  }
}

void IcsParser::addInstance(ts_t st, ts_t en, bool allDay) {
  if (_n == KEEP && st >= _ev[_n - 1].start) return;
  int pos = 0;
  while (pos < _n && _ev[pos].start <= st) pos++;
  int last = (_n < KEEP) ? _n : KEEP - 1;
  for (int j = last; j > pos; j--) _ev[j] = _ev[j - 1];
  if (_n < KEEP) _n++;
  CalEvent &e = _ev[pos];
  e.start = st; e.end = en; e.allDay = allDay; e.uid = _p.uid; e.orig = st;
  strncpy(e.title, _p.title, CAL_TITLE_LEN - 1);
  e.title[CAL_TITLE_LEN - 1] = 0;
}

bool IcsParser::consider(long dn) {
  const Rule &r = _p.rr;
  const ts_t wall = (ts_t)dn * 86400 + _x.secOfDay;
  if (wall < _x.startWall) return true;
  _occ++;
  if (r.freq && r.count && _occ > r.count) return false;
  if (wall > _x.hiWall) return false;
  if (wall < _x.loWall) return true;
  const ts_t st = calZoneLocalToUtc(_x.zone, wall);
  if (r.freq && r.hasUntil && st > r.until) return false;
  ts_t en = _x.allDay ? calZoneLocalToUtc(_x.zone, wall + (ts_t)_x.allDayDays * 86400) : st + _x.durSec;
  const ts_t endEff = (en > st) ? en : st + 900;       // an event with no length stays for 15 minutes
  if (endEff <= _lo || st >= _hi) return true;
  for (int i = 0; i < _p.nEx; i++) if (_p.exd[i] == st) return true;
  if (!_p.haveRec && overridden(_p.uid, st)) return true;
  addInstance(st, en, _x.allDay);
  return true;
}

// Does day d of month m in year y match the rule's BYDAY / BYMONTHDAY (or the start's day of month)?
bool IcsParser::daySelected(int y, int m, int d, int startDay) const {
  const Rule &r = _p.rr;
  const int dim = calDaysInMonth(y, m);
  if (!r.nByDay && !r.nByMonthDay) return d == startDay;
  bool okMd = true, okBd = true;
  if (r.nByMonthDay) {
    okMd = false;
    for (int i = 0; i < r.nByMonthDay; i++) { int v = r.byMonthDay[i]; if (v > 0 ? v == d : dim + 1 + v == d) okMd = true; }
  }
  if (r.nByDay) {
    okBd = false;
    const int wd = calWeekday(calDaysFromCivil(y, m, d));
    for (int i = 0; i < r.nByDay; i++) {
      if (r.byDay[i].wd != wd) continue;
      const int ord = r.byDay[i].ord;
      if (ord == 0 || (ord > 0 && (d - 1) / 7 + 1 == ord) || (ord < 0 && (dim - d) / 7 + 1 == -ord)) okBd = true;
    }
  }
  return okMd && okBd;
}

void IcsParser::expand() {
  const Rule &r = _p.rr;
  Exp &x = _x;
  x = Exp();
  CalTm t;
  calBreak(_p.start.wall, t);
  x.zone = _p.start.zone;
  x.allDay = _p.start.dateOnly;
  x.secOfDay = t.hour * 3600 + t.min * 60 + t.sec;
  x.startWall = _p.start.wall;
  x.startDay = t.day;
  if (x.allDay) {
    int days = _p.haveEnd ? (int)((_p.end.wall - _p.start.wall) / 86400) : _p.haveDur ? (int)(_p.durSec / 86400) : 1;
    if (days < 1) days = 1;
    x.allDayDays = days;
    x.durSec = (long)days * 86400;
  } else {
    long dur = _p.haveEnd ? (long)(_p.end.utc - _p.start.utc) : _p.haveDur ? _p.durSec : 0;
    x.durSec = dur < 0 ? 0 : dur;
  }
  x.loWall = _lo - x.durSec - 86400;
  x.hiWall = _hi + 86400;
  _occ = 0;

  const long dn0 = calDaysFromCivil(t.year, t.mon, t.day);
  if (!r.freq) { consider(dn0); return; }

  const long loDn = floorDiv(x.loWall, 86400);
  const long hiDn = floorDiv(x.hiWall, 86400) + 1;
  const bool skip = (r.count == 0);                  // with a COUNT every repeat has to be counted
  long steps = 0;
  const long MAXSTEPS = 30000;
  bool go = true;
  const long iv = r.interval;

  if (r.freq == 1) {                                 // daily
    long k = 0;
    if (skip && loDn > dn0) k = (loDn - dn0) / iv - 1;
    if (k < 0) k = 0;
    for (; go && steps < MAXSTEPS; k++, steps++) {
      const long dn = dn0 + k * iv;
      if (dn > hiDn) break;
      int y, m, d; calCivilFromDays(dn, y, m, d);
      if (r.byMonth && !((r.byMonth >> m) & 1)) continue;
      if (r.nByDay) {
        bool ok = false; const int wd = calWeekday(dn);
        for (int i = 0; i < r.nByDay; i++) if (r.byDay[i].wd == wd) ok = true;
        if (!ok) continue;
      }
      go = consider(dn);
    }
  } else if (r.freq == 2) {                          // weekly
    const long ws = dn0 - ((calWeekday(dn0) - r.wkst + 7) % 7);
    const int startWd = calWeekday(dn0);
    long k = 0;
    if (skip && loDn > ws) k = (loDn - ws) / (7 * iv) - 1;
    if (k < 0) k = 0;
    for (; go && steps < MAXSTEPS; k++, steps++) {
      const long base = ws + 7 * k * iv;
      if (base > hiDn) break;
      for (int i = 0; i < 7 && go; i++) {
        const long dn = base + i;
        const int wd = calWeekday(dn);
        bool ok = false;
        if (r.nByDay) { for (int j = 0; j < r.nByDay; j++) if (r.byDay[j].wd == wd) ok = true; }
        else ok = (wd == startWd);
        if (!ok) continue;
        if (r.byMonth) { int y, m, d; calCivilFromDays(dn, y, m, d); if (!((r.byMonth >> m) & 1)) continue; }
        go = consider(dn);
      }
    }
  } else {                                           // monthly (3) and yearly (4)
    int ly, lm, ld; calCivilFromDays(loDn, ly, lm, ld);
    long k = 0;
    if (skip) {
      if (r.freq == 3) { long span = ((long)ly * 12 + lm - 1) - ((long)t.year * 12 + t.mon - 1); if (span > 0) k = span / iv - 1; }
      else { long span = ly - t.year; if (span > 0) k = span / iv - 1; }
    }
    if (k < 0) k = 0;
    for (; go && steps < MAXSTEPS; k++, steps++) {
      int months[12], nm = 0, y;
      if (r.freq == 3) {
        const long mi = (long)t.year * 12 + (t.mon - 1) + k * iv;
        y = (int)(mi / 12);
        const int m = (int)(mi % 12) + 1;
        if (r.byMonth && !((r.byMonth >> m) & 1)) { if (calDaysFromCivil(y, m, 1) > hiDn) break; continue; }
        months[nm++] = m;
      } else {
        y = (int)(t.year + k * iv);
        if (r.byMonth) { for (int m = 1; m <= 12; m++) if ((r.byMonth >> m) & 1) months[nm++] = m; }
        else months[nm++] = t.mon;
      }
      if (calDaysFromCivil(y, months[0], 1) > hiDn) break;
      for (int mi = 0; mi < nm && go; mi++) {
        const int m = months[mi];
        const long first = calDaysFromCivil(y, m, 1);
        const int dim = calDaysInMonth(y, m);
        for (int d = 1; d <= dim && go; d++)
          if (daySelected(y, m, d, x.startDay)) go = consider(first + d - 1);
      }
    }
  }
}

void IcsParser::finishEvent() {
  _seen++;
  if (!_p.start.ok) return;
  if (_p.haveRec) {                                  // this event replaces one repeat of a repeating event
    _ovr[_ovrNext] = {_p.uid, _p.recId};
    _ovrNext = (_ovrNext + 1) % MAX_OVR;
    if (_nOvr < MAX_OVR) _nOvr++;
    dropInstance(_p.uid, _p.recId);
  }
  if (_p.cancelled) return;
  if (!_p.title[0]) strcpy(_p.title, "(no title)");
  expand();
}

// ======================= what to show =======================
static const char *const DAY_NAMES[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MON_NAMES[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void fmtTime(char *out, size_t cap, const CalTm &t, bool h24) {
  if (h24) { snprintf(out, cap, "%d:%02d", t.hour, t.min); return; }
  int h = t.hour % 12; if (!h) h = 12;
  if (t.min) snprintf(out, cap, "%d:%02d%c", h, t.min, t.hour < 12 ? 'A' : 'P');
  else       snprintf(out, cap, "%d%c", h, t.hour < 12 ? 'A' : 'P');
}

static void fmtDay(char *out, size_t cap, long diff, const CalTm &t) {
  if (diff <= 0) snprintf(out, cap, "TODAY");
  else if (diff == 1) snprintf(out, cap, "TOMORROW");
  else if (diff < 7) snprintf(out, cap, "%s", DAY_NAMES[t.wday]);
  else snprintf(out, cap, "%s %d", MON_NAMES[(t.mon - 1) % 12], t.day);
}

void calHeader(const CalEvent &e, ts_t now, const CalZone &b, bool h24, char *out, size_t cap, bool *inProgress) {
  const ts_t nowL = now + calZoneOffsetUtc(b, now);
  const ts_t stL = e.start + calZoneOffsetUtc(b, e.start);
  const ts_t enL = e.end + calZoneOffsetUtc(b, e.end);
  const long diff = floorDiv(stL, 86400) - floorDiv(nowL, 86400);
  const bool started = e.start <= now;
  if (inProgress) *inProgress = started;
  CalTm st; calBreak(stL, st);
  char day[16], tm[12];
  fmtDay(day, sizeof(day), diff, st);

  if (e.allDay) {
    if (strlen(day) + 8 <= 14) snprintf(out, cap, "%s ALL DAY", day);
    else snprintf(out, cap, "%s", day);
  } else if (started) {
    if (e.end <= e.start) { snprintf(out, cap, "NOW"); return; }
    CalTm en; calBreak(enL, en);
    if (floorDiv(enL, 86400) == floorDiv(nowL, 86400)) { fmtTime(tm, sizeof(tm), en, h24); snprintf(out, cap, "NOW TO %s", tm); }
    else { char d2[16]; fmtDay(d2, sizeof(d2), floorDiv(enL, 86400) - floorDiv(nowL, 86400), en); snprintf(out, cap, "NOW TO %s", d2); }
  } else if (e.start - now < 3600) {
    long mins = (long)((e.start - now + 59) / 60);
    if (mins < 1) mins = 1;
    snprintf(out, cap, "IN %ld MIN", mins);
  } else {
    fmtTime(tm, sizeof(tm), st, h24);
    snprintf(out, cap, "%s %s", day, tm);
  }
}

int calWrap(const char *text, int cols, int maxLines, char lines[][CAL_MAX_COLS + 1]) {
  if (cols > CAL_MAX_COLS) cols = CAL_MAX_COLS;
  int n = 0;
  const char *p = text;
  while (*p == ' ') p++;
  while (*p && n < maxLines) {
    int len = (int)strlen(p);
    int cut;
    bool hyphen = false;                             // a word longer than the line is split with a hyphen
    if (len <= cols) cut = len;
    else {
      int sp = -1;
      for (int i = 1; i <= cols && p[i]; i++) if (p[i] == ' ') sp = i;
      if (sp > 0) cut = sp;
      else if (cols >= 5) { cut = cols - 1; hyphen = true; }
      else cut = cols;
    }
    int w = cut;
    while (w > 0 && p[w - 1] == ' ') w--;
    memcpy(lines[n], p, w);
    if (hyphen) lines[n][w++] = '-';
    lines[n][w] = 0;
    n++;
    p += cut;
    while (*p == ' ') p++;
  }
  if (*p && n > 0) {                                 // more text than room: end the last line with ".."
    char *l = lines[n - 1];
    int len = (int)strlen(l);
    if (len > cols - 2) len = cols - 2;
    while (len > 0 && l[len - 1] == ' ') len--;
    l[len] = 0;
    strcat(l, "..");
  }
  return n;
}
