#pragma once
// Calendar data helpers with no hardware in them (so they can be tested on a PC):
//   * date arithmetic and time zones (POSIX rules, plus a table that turns the zone names an
//     Apple calendar uses, like "America/Los_Angeles", into rules),
//   * a streaming reader for iCalendar (.ics) files, including repeating events, that keeps
//     only the next few upcoming events no matter how big the calendar is,
//   * the text shown on the panel (header line, wrapped title).

#include <stdint.h>
#include <stddef.h>

typedef int64_t ts_t;                 // seconds since 1970-01-01 00:00 UTC

// ---------- dates ----------
long calDaysFromCivil(int y, int m, int d);                 // days since 1970-01-01
void calCivilFromDays(long z, int &y, int &m, int &d);
int  calDaysInMonth(int y, int m);
int  calWeekday(long days);                                 // 0 = Sunday
ts_t calEpoch(int y, int m, int d, int h, int mi, int s);   // the fields read as UTC

struct CalTm { int year, mon, day, hour, min, sec, wday; }; // mon 1-12, wday 0 = Sunday
void calBreak(ts_t t, CalTm &out);                          // split a count of seconds into fields

// ---------- time zones ----------
struct CalZone {
  int  stdOff = 0;                    // seconds east of UTC in standard time
  int  dstOff = 0;                    // seconds east of UTC in daylight time
  bool hasDst = false;
  int  sm = 0, sw = 0, sd = 0, st = 0;    // daylight time starts: month, week (5 = last), weekday, seconds after midnight
  int  em = 0, ew = 0, ed = 0, et = 0;    // and ends
};
bool  calZoneParse(const char *posix, CalZone &z);          // "PST8PDT,M3.2.0,M11.1.0"
int   calZoneOffsetUtc(const CalZone &z, ts_t utc);         // local = utc + offset
ts_t  calZoneLocalToUtc(const CalZone &z, ts_t wall);       // wall = local time read as UTC seconds
const char *calIanaToPosix(const char *tzid);               // nullptr if the name is not known

// ---------- events ----------
static const int CAL_TITLE_LEN = 64;

struct CalEvent {
  ts_t     start = 0, end = 0;        // UTC. An all-day event runs from local midnight to local midnight.
  bool     allDay = false;
  uint32_t uid = 0;                   // hash of the event's UID
  ts_t     orig = 0;                  // start of the repeat this came from (to drop moved repeats)
  char     title[CAL_TITLE_LEN] = "";
};

// Feed the calendar file one character at a time. Repeating events are expanded and only the
// KEEP earliest instances that are still to come (between winLo and winHi) are remembered.
class IcsParser {
 public:
  static const int KEEP = 24;
  void begin(ts_t winLo, ts_t winHi, const char *localPosix);
  void feed(char c);
  void finish();                                             // after the last character
  int  count() const { return _n; }
  const CalEvent &event(int i) const { return _ev[i]; }      // sorted by start time
  bool sawCalendar() const { return _sawCal; }
  int  eventsSeen() const { return _seen; }

 private:
  static const int LINE_LEN = 600;
  static const int MAX_EX = 16, MAX_BYDAY = 7, MAX_BYMD = 8, MAX_OVR = 40;

  struct Dt { ts_t wall = 0, utc = 0; bool dateOnly = false; bool ok = false; CalZone zone; };
  struct ByDay { int8_t ord; int8_t wd; };
  struct Rule {
    int freq = 0;                     // 0 none/unsupported, 1 daily, 2 weekly, 3 monthly, 4 yearly
    int interval = 1;
    int count = 0;
    bool hasUntil = false; ts_t until = 0;
    ByDay byDay[MAX_BYDAY]; int nByDay = 0;
    int8_t byMonthDay[MAX_BYMD]; int nByMonthDay = 0;
    uint16_t byMonth = 0;             // bit m for month m (1-12)
    int wkst = 1;                     // week starts on: 0 Sunday .. 6 Saturday (default Monday)
  };
  struct Pend {
    char title[CAL_TITLE_LEN]; Dt start, end; bool haveEnd = false; long durSec = 0; bool haveDur = false;
    bool cancelled = false; Rule rr; ts_t exd[MAX_EX]; int nEx = 0;
    bool haveRec = false; ts_t recId = 0; uint32_t uid = 0;
  };
  struct Ovr { uint32_t uid; ts_t recId; };
  struct Exp {                                  // what expand() is working on
    CalZone zone; long durSec = 0; int allDayDays = 0; bool allDay = false;
    int secOfDay = 0; ts_t startWall = 0, loWall = 0, hiWall = 0; int startDay = 1;
  };

  ts_t _lo = 0, _hi = 0;
  CalZone _board;
  bool _sawCal = false, _inEvent = false, _inAlarm = false;
  int  _seen = 0;
  char _phys[LINE_LEN]; int _physLen = 0;
  char _line[LINE_LEN]; int _lineLen = 0;
  Pend _p;
  int  _occ = 0;
  Exp  _x;
  CalEvent _ev[KEEP]; int _n = 0;
  Ovr _ovr[MAX_OVR]; int _nOvr = 0, _ovrNext = 0;

  void physLine();
  void processLine(char *line);
  void finishEvent();
  bool parseDt(const char *params, const char *val, Dt &dt);
  void parseRule(char *val);
  void expand();
  bool consider(long dn);                       // one candidate day; false = stop generating
  bool overridden(uint32_t uid, ts_t startUtc) const;
  void addInstance(ts_t st, ts_t en, bool allDay);
  void dropInstance(uint32_t uid, ts_t orig);
  bool daySelected(int y, int m, int d, int startDay) const;
};

// ---------- what to show ----------
// One line for the top band, e.g. "TODAY 3:30P", "TOMORROW 9A", "NOW TO 4:30P", "IN 25 MIN",
// "WED ALL DAY", "SEP 23 7P". Sets *inProgress when the event has already started.
void calHeader(const CalEvent &e, ts_t now, const CalZone &board, bool h24, char *out, size_t cap, bool *inProgress);
// Word-wraps text into at most maxLines lines of cols characters (last line ends in ".." if cut).
static const int CAL_MAX_COLS = 16;
int  calWrap(const char *text, int cols, int maxLines, char lines[][CAL_MAX_COLS + 1]);
