#pragma once
// Calendar module: shows your upcoming events, one per page, from a published (public)
// calendar. In the Calendar app on a Mac, iPhone or iCloud.com: share the calendar as a
// "Public Calendar" and copy the link (it starts with webcal://). Paste that link on the
// settings page. Any other calendar that offers an .ics link works too (Google, Outlook...).
//
// The calendar file is downloaded in a background task, read as a stream (so a big calendar
// is fine), repeating events are worked out, and only the next few events are kept.

#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "CalData.h"

class CalendarModule : public Module {
 public:
  CalendarModule() : Module("calendar", "Calendar", "k_", true, 8) {}
  const char *version() const override { return "1.1.1"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                     // when the minute changes ("IN 25 MIN")

  const char *enabledLabel() override { return "Show upcoming events in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // ---- settings ----
  String   _url = "";
  uint8_t  _daysIdx = 3;                           // index into DAYS_AHEAD
  uint8_t  _maxEvents = 5;
  uint8_t  _everyIdx = 1;                          // index into EVERY_MIN
  bool     _h24 = false;
  bool     _showAllDay = true;
  bool     _showNow = true;                        // events already under way
  uint32_t _colBand = 0xE0301E, _colBandText = 0xFFFFFF, _colNow = 0x1E9E4A;

  // ---- data (written by the background task, read by the display) ----
  SimpleLock _lock;
  CalEvent   _ev[IcsParser::KEEP];
  int        _evN = 0;
  bool       _tried = false, _failed = false;
  char       _status[96] = "";
  unsigned long _lastTry = 0;

  // ---- background task ----
  volatile bool _fetchNow = false;
  IcsParser    *_parser = nullptr;                // only exists while a download is being read (it is big, and the
                                                  // memory the secure connection needs is scarce)
  bool   fetch();
  bool   fetchInner();
  void   setStatus(const char *s);

  // ---- display ----
  long _lastMinute = -1;
  unsigned long _lastScrollStep = 0;
  int  visible(CalEvent *out, int max);            // events still to show, in order
};
