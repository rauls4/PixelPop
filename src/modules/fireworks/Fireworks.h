#pragma once
// Fireworks module: an animated fireworks show over a small city skyline.
// Rockets rise, burst into spheres, rings and golden "willows", and (optionally)
// the show ends with a finale just before the page rotates away.
// No data or network needed; everything is simulated on the board.
// Optional sound: a whoosh when a rocket goes up, a boom when it bursts and a
// crackle of falling sparks, played on the onboard speaker. Chimes take priority.

#include "../../core/Module.h"
#include "FireworkSound.h"

class FireworksModule : public Module {
 public:
  FireworksModule() : Module("fireworks", "Fireworks", "f_", true, 10) {}
  const char *version() const override { return "1.0.0"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override { (void)sub; return true; }
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show fireworks in the rotation"; }
  String summary() override;
  String actionsHtml() override;
  void   registerRoutes(WebServer &server) override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t  _palette = 0;        // 0 multicolor, 1 red/white/blue, 2 gold & white, 3 your color
  uint8_t  _density = 1;        // 0 slow, 1 medium, 2 busy
  bool     _finale = true;
  bool     _skyline = true;
  uint32_t _colCustom = 0xFF3030;
  bool     _sound = false;              // play sound with the show
  uint8_t  _soundVol = 50;              // 1..100

  // simulation
  struct Rocket { bool on; float x, y, vx, vy, apexY; uint8_t r, g, b; };
  struct Spark  { bool on; bool glitter; float x, y, vx, vy, life, maxLife, grav; uint8_t r, g, b; };
  static const int MAX_ROCKETS = 4;
  static const int MAX_SPARKS = 160;
  Rocket _rk[MAX_ROCKETS];
  Spark  _sp[MAX_SPARKS];

  uint32_t      _rng = 2463534242u;
  volatile unsigned long _lastFrame = 0;   // time of the last drawn frame
  unsigned long _showStart = 0;       // when this run of the show began
  unsigned long _nextLaunch = 0;
  float         _flashX = 0, _flashY = 0;
  int           _flash = 0;           // frames of burst flash left

  // sound (played by its own task while the page is on screen)
  struct SndEvent { uint8_t type; uint8_t level10; uint16_t ms; };
  SndEvent      _q[16];
  volatile uint8_t _qHead = 0, _qTail = 0;   // main thread pushes at head, sound task pops at tail
  volatile bool _soundRunning = false;
  volatile unsigned long _testStartMs = 0;   // set by the Test button
  unsigned long _soundRetryAt = 0;
  char          _soundStatus[96] = "";

  void pushSound(FwSound s, float level, float seconds = 1.0f);
  void startSound();
  void soundTask();
  static void soundEntry(void *arg);

  uint32_t rnd();
  float    frand(float a, float b);
  void     resetShow(unsigned long now);
  void     launch();
  void     burst(const Rocket &r);
  void     addSpark(float x, float y, float ang, float speed, float life, float grav,
                    uint8_t r, uint8_t g, uint8_t b, bool glitter);
  void     pickColor(uint8_t &r, uint8_t &g, uint8_t &b);
  void     step(float dt, unsigned long now);
  void     render();
  void     drawSkyline();
};
