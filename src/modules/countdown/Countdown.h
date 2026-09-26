#pragma once
// Countdown module: counts down to a date, with a label you choose
// (e.g. "Vacation", "Birthday"). Set the label and date on its settings page.
// Shows a label and the whole days remaining until the selected date.

#include "../../core/Module.h"

class CountdownModule : public Module {
 public:
  CountdownModule() : Module("countdown", "Countdown", "d_", true, 5) {}
  const char *version() const override { return "1.3.8"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show the countdown in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void   registerRoutes(WebServer &server) override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  String   _label = "New Year";      // up to 10 characters
  String   _date = "2027-01-01";     // YYYY-MM-DD
  bool     _hidePassed = false;      // stop showing once the date has passed
  bool     _repeat = false;          // repeat every year (birthdays, holidays)
  bool     _hourglass = true;        // the animated hourglass shown alongside the day count
  uint32_t _colLabel = 0xFFD200, _colNumber = 0xFFFFFF, _colAccent = 0x00C8FF;

  long _lastKey = -1;                // what is on screen (second or day), to know when to redraw
  bool _availabilityKnown = false;
  bool _lastAvailable = false;

  bool dateParts(int &y, int &m, int &d) const;
  bool daysLeft(long &days, long &today) const;   // false if time not synced / date invalid
  bool resolveTarget(const struct tm &now, int &y, int &m, int &d) const;   // applies "repeat"
  bool remaining(const struct tm &now, long &days, long &hours, bool &past, bool &today) const;
  bool showHourglass(bool past, bool today) const;
  long redrawKey(const struct tm &t) const;       // changes whenever the page needs redrawing
  void drawTwoRows(const struct tm &now);          // landscape day count and optional hourglass
  void drawPortrait(const struct tm &now);         // vertical layout for the 32 x 64 canvas, optional hourglass row
  void drawHourglass(int x, int y);
};
