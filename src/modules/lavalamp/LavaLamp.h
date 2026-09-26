#pragma once
// Lava Flow module: soft, additive color blobs with fading trails.

#include "../../core/Module.h"
#include "../../core/Config.h"

class LavaLampModule : public Module {
 public:
  LavaLampModule() : Module("lavalamp", "Lava Flow", "ll_", true, 10) {}
  const char *version() const override { return "1.7.1"; }

  bool pageAvailable(int sub) override { (void)sub; return true; }
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show the lava screensaver in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void registerRoutes(WebServer &server) override;
  bool showSettingsPreview() override { return true; }

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  uint8_t  _speed = 100;               // 25..200 percent
  uint8_t  _blobCount = 8;             // 1..BLOB_COUNT
  uint8_t  _blobSize = 100;            // 50..150 percent
  uint8_t  _trailFade = 188;           // 120..235, higher = longer persistence
  uint8_t  _cohesion = 35;             // 0..100 percent
  uint8_t  _convection = 100;          // 0..200 percent
  uint8_t  _sizeVariation = 35;        // 0..100 percent
  bool     _randomColors = false;
  bool     _showClock = false;
  uint32_t _colA = 0xFF1400;
  uint32_t _colB = 0xFF0037;
  uint32_t _colC = 0x6400FF;
  uint32_t _glow = 0x100018;
  unsigned long _lastFrame = 0;
  uint32_t _rng = 0xB10B5EED;
  int _frameW = 0, _frameH = 0;

  struct Blob {
    float along, cross, velAlong, velCross, temperature, radius;
    uint32_t color;
  };
  static const int BLOB_COUNT = 12;
  Blob _blobs[BLOB_COUNT];
  uint32_t _frame[PANEL_W * PANEL_H] = {};

  void render();
  void resetBlobs(int width, int height, bool portrait);
  uint32_t nextRandom();
  uint32_t nextBlobColor(uint8_t paletteIndex);
  float nextRadius();
};
