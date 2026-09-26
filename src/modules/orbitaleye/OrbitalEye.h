#pragma once
// A silent, original sci-fi visual: a red lens beside a green CRT terminal.

#include "../../core/Module.h"

class OrbitalEyeModule : public Module {
 public:
  OrbitalEyeModule() : Module("orbitaleye", "HAL", "oe_", false, 8) {}
  const char *version() const override { return "1.3.0"; }

  bool pageAvailable(int sub) override { (void)sub; return true; }
  void drawPage(int sub) override;
  bool needsRedraw() override;
  String summary() override { return "Silent mission-control eye"; }

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  uint32_t _color = 0xF02020;
  uint8_t  _speed = 1;
  uint8_t  _terminalScrollSpeed = 22;
  String   _terminalLines;
  bool     _portraitTerminal = false;
  unsigned long _lastFrame = 0;

  void drawLandscapeTerminal(unsigned long now, int x, int width);
  void drawPortraitTerminal(unsigned long now);
};
