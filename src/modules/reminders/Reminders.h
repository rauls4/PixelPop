#pragma once
// Reminders module: shows the things you still have to do, one reminder per page: a colored band
// across the top says when it is due (red = overdue, amber = today), the title scrolls if it is too
// long for the screen, and a small line under it gives the due date and the list it belongs to.
// High priority reminders get a blinking exclamation mark in the band.
//
// Apple no longer lets other devices read the Reminders app directly, so the list is SENT to the
// board: a Shortcut on your iPhone or Mac gathers your open reminders and posts them to
// http://pixelpop.local/reminders/push (the settings page has the exact address and the steps).
// The list is kept in flash, so it is still there after a restart.

#include "../../core/Module.h"
#include "RemData.h"

class RemindersModule : public Module {
 public:
  RemindersModule() : Module("reminders", "Reminders", "n_", true, 8) {}
  const char *version() const override { return "1.0.0"; }      // bump when this module changes (see CHANGELOG.md)

  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                     // scrolling text, the blinking !, and when the minute changes

  const char *enabledLabel() override { return "Show reminders in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // ---- settings ----
  String   _key = "";                              // must be sent with the list (keeps others on the network out)
  uint8_t  _window = 3;                            // index into WINDOW_DAYS
  bool     _showUndated = true;
  uint8_t  _maxItems = 5;
  bool     _h24 = false;
  bool     _urgentOver = false;                    // overdue reminders blink the ! too
  bool     _showDone = true;                       // "ALL CLEAR" page when there is nothing to do
  uint8_t  _staleIdx = 3;                          // index into STALE_HOURS
  uint32_t _colOver = 0xD62A2A, _colToday = 0xE08A00, _colBand = 0x2F6FD6, _colTitle = 0xFFFFFF, _colDetail = 0x9DB0CC;

  // ---- the list ----
  String   _raw;                                   // exactly as received (also what is saved)
  RemItem  _items[REM_MAX];
  int      _n = 0;
  bool     _pushed = false;                        // a list has arrived at least once
  uint32_t _pushedAt = 0;                          // when (seconds since 1970), 0 = clock was not set
  uint32_t _savedAt = 0;
  long     _lastMinute = -1;

  // ---- animation (scrolling text and the blinking !) ----
  unsigned long _scrollT0 = 0, _lastDraw = 0, _lastFrame = 0;
  long          _lastPhase = -1;
  int           _lastSub = -1;
  bool          _scrolling = false, _blinking = false;    // what the page drawn last needs
  void field(const char *s, int x0, int x1, int y, bool small, uint16_t col, unsigned long now);

  void setItems(const String &text);
  bool stale() const;
  ts_t nowWall() const;                            // local time as UTC seconds, 0 if the clock is not set
  int  visible(RemItem *out, int max);
  void drawDone();
  void drawItem(const RemItem &r, ts_t now);
};
