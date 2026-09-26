#pragma once
// Menu of the Week module: what is for lunch or dinner, one page per day, from a published calendar.
// Put the menu in a calendar (one event per dish or meal, the event title is the text shown; a
// whole-day event or a timed one both work) and paste that calendar's webcal:// address on the
// settings page. Repeating events work too. A day with a long menu scrolls up the screen, and the
// page stays until the whole menu has been read.
//
// It reads the calendar the same way the Calendar module does (see CalData.h) but keeps its own
// address and settings, so the menu can live in a different calendar from your appointments.

#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "../calendar/CalData.h"

class MenuModule : public Module {
 public:
  MenuModule() : Module("menu", "Menu of the Week", "j_", true, 12) {}
  const char *version() const override { return "1.0.1"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                     // a long menu scrolls; the day label changes at midnight
  int  pageProgress() override;                    // a long menu stays until it has been read

  const char *enabledLabel() override { return "Show the menu in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // ---- settings ----
  String   _url = "";
  uint8_t  _spanIdx = 4;                           // how many days: index into SPAN_NAMES
  uint8_t  _everyIdx = 1;                          // index into EVERY_MIN
  uint8_t  _speed = 1;                             // 0 slow, 1 normal, 2 fast (long menus)
  uint32_t _colBand = 0x8A3FD0, _colBandText = 0xFFFFFF, _colToday = 0xE0301E, _colA = 0xFFFFFF, _colB = 0xFFD060;

  // ---- data (written by the background task, read by the display) ----
  SimpleLock _lock;
  CalEvent   _ev[IcsParser::KEEP];
  int        _evN = 0;
  bool       _tried = false, _failed = false;
  char       _status[96] = "";
  unsigned long _lastTry = 0;

  // ---- background task ----
  volatile bool _fetchNow = false;
  IcsParser    *_parser = nullptr;                // only exists while a download is being read
  bool   fetch();
  bool   fetchInner();
  void   setStatus(const char *s);

  // ---- display ----
  static const int MAXPER = 6;                     // dishes on one day
  struct DayView {
    long day = 0;                                  // the local day, in days since 1970-01-01
    int  ahead = 0;                                // 0 = today, 1 = tomorrow ...
    int  n = 0;
    char item[MAXPER][CAL_TITLE_LEN];
  };
  int  spanDays(long today) const;
  int  scan(int want, DayView *out);               // the days with a menu; fills *out with the want-th one; returns how many days
  long          _lastMinute = -1;
  int           _curSub = -1;
  unsigned long _t0 = 0, _lastSeen = 0, _totalMs = 0, _stepMs = 60;
  int           _scrollDist = 0, _lastOffset = -1;
  bool          _scrollable = false;
  unsigned long msPerPx() const;
};
