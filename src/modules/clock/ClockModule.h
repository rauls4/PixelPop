#pragma once
// Clock module: time (12/24 h), date, and a seconds bar along the bottom edge.
// The time zone is a general setting on the main page.

#include "../../core/Module.h"

class ClockModule : public Module {
 public:
  ClockModule() : Module("clock", "Clock", "c_", true, 15) {}
  const char *version() const override { return "1.4.2"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show the clock in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void   registerRoutes(WebServer &server) override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  bool     _use24h = false;
  bool     _showDate = true;
  bool     _showBar = true;
  bool     _pendulumSound = false;
  uint32_t _colTime = 0xFFFFFF, _colAccent = 0x00C8FF;

  int _lastSec = -1;
  int _lastPendulumSec = -1;
  unsigned long _lastFrame = 0;
  volatile bool _pendulumSoundRunning = false;
  bool _pendulumHigh = false;

  void drawPortrait(const struct tm &t);
  void startPendulumSound(bool high);
  static void pendulumTaskEntry(void *arg);
  void playPendulumSound();
};
