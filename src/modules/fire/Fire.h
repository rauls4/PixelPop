#pragma once
// Fire: a self-contained heat simulation, adapted for the HUB75 canvas.

#include "../../core/Module.h"
#include "../../core/Config.h"

class FireModule : public Module {
 public:
  FireModule() : Module("fire", "Fire", "fi_", true, 12) {}
  const char *version() const override { return "1.1.0"; }

  bool pageAvailable(int sub) override { (void)sub; return true; }
  void drawPage(int sub) override;
  bool needsRedraw() override;
  const char *enabledLabel() override { return "Show the fire animation in the rotation"; }
  String summary() override;

 protected:
  void onLoad() override;
  String onSettingsHtml() override;
  void onSave(WebServer &server) override;

 private:
  uint8_t _cooling = 65;
  uint8_t _sparking = 125;
  uint8_t _sparkHeight = 4;
  uint8_t _roast = 1;             // 0 off, 1 random, 2 marshmallow, 3 sausage
  // Portrait has a 64-pixel logical height, so both axes use the largest
  // panel dimension even though landscape only needs 32 rows.
  uint8_t _heat[PANEL_W][PANEL_W] = {};
  uint32_t _rng = 0xF1A3C0DE;
  unsigned long _lastFrame = 0;
  unsigned long _roastStarted = 0;
  int _roastX = 0;
  uint8_t _roastKind = 2;
  int _simW = 0, _simH = 0;

  uint32_t nextRandom();
  void reset(int width, int height);
  uint16_t heatColor(uint8_t heat) const;
  void drawRoast(int width, int height);
};
