#pragma once
// Marquee module: your own message, one to three lines, inside a border of
// "dancing ants" - a ring of little dashes that keeps marching around the edge
// of the screen, like the lights on a theater sign.
//
// Lines that are too long for the screen pause, then scroll sideways. Text,
// colors, ant style, speed and direction are set on the module's settings page.

#include "../../core/Module.h"

class MarqueeModule : public Module {
 public:
  MarqueeModule() : Module("marquee", "Marquee", "m_", true, 8) {}
  const char *version() const override { return "1.0.0"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                    // every ant step, or faster while text scrolls

  const char *enabledLabel() override { return "Show the marquee in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void   registerRoutes(WebServer &server) override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  static const int MQ_LINE_MAX = 40;

  String   _line[3];                       // empty lines are skipped
  bool     _onlyDay = false;               // show on one date only
  String   _day = "";                      // that date, YYYY-MM-DD
  uint8_t  _style = 0;                     // 0 marching dashes, 1 dots, 2 long dashes
  uint8_t  _speed = 1;                     // 0 slow, 1 normal, 2 fast
  uint8_t  _dir = 0;                       // 0 clockwise, 1 counter-clockwise
  uint32_t _colText = 0xFFFFFF, _colAnts = 0xFFB000, _colGap = 0x000000;

  bool          _scrolling = false;        // some line is wider than the screen
  unsigned long _scrollT0 = 0, _lastDraw = 0;
  unsigned long _lastKey = 0;         // the ant step on screen
  unsigned long _lastText = 0;        // the text scroll step on screen

  bool isToday(const String &date) const;  // false if the time is not known yet
  static bool todayString(String &out);
  int  antMs() const;
  void drawAnts(unsigned long step);
  int  lineCount() const;
};
