#pragma once
// Chomper module: a little animated story along a maze corridor, with original
// characters: a yellow eyeless critter (the chomper) and four two-eyed wisps.
//   1. The chomper runs across the screen eating dots with the four wisps
//      (violet, teal, gold, magenta) chasing close behind.
//   2. At the end of the corridor it grabs a glowing star: the wisps turn blue and scared.
//   3. The chomper turns around and chases the scared wisps, gobbling them one by
//      one (100, 200, 400, 800 points). Their eyes race away.
//   4. A short pause, then it starts over. Runs on the board, no data needed.

#include "../../core/Module.h"

class ChomperModule : public Module {
 public:
  ChomperModule() : Module("chomper", "Arcade", "p_", true, 12) {}
  const char *version() const override { return "1.2.5"; }      // bump when this module changes (see CHANGELOG.md)

  bool pageAvailable(int sub) override { (void)sub; return true; }
  int pageProgress() override;
  void pageEntered(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show the chomper in the rotation"; }
  String summary() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t  _speed = 1;              // 0 slow, 1 normal, 2 fast
  bool     _showScore = true;
  bool     _showLives = true;
  uint32_t _colWall = 0x2030FF;

  // the show
  enum Stage : uint8_t { CHASE, HUNT, EXIT, PAUSE };
  enum WispMode : uint8_t { NORMAL, FRIGHT, EYES, GONE };
  static const int NWISP = 4;
  static const int MAX_DOTS = 20;

  Stage     _stage = CHASE;
  float     _px = 0;                // the chomper's center x
  int       _dir = 1;               // +1 right, -1 left
  float     _wx[NWISP];
  WispMode _wm[NWISP];
  bool      _dot[MAX_DOTS];
  int       _nDots = 0;
  float     _pelletX = 0;
  bool      _pellet = true;
  float     _anim = 0;              // drives mouth and feet
  float     _clock = 0;
  float     _freeze = 0;            // seconds of "freeze" left (after eating something)
  int       _popWisp = -1;         // ghost replaced by its score while frozen
  char      _popText[6] = "";
  float     _popX = 0;
  float     _pause = 0;
  int       _eaten = 0;
  int       _score = 0;

  bool          _started = false;
  bool          _landscapeComplete = false;
  unsigned long _lastFrame = 0;

  // Portrait uses an autonomous falling-block game instead of the horizontal chase.
  static const int TETRA_W = 8, TETRA_H = 20;
  uint8_t _tetra[TETRA_H][TETRA_W] = {};
  uint8_t _tetraKind = 0, _tetraRot = 0;
  int8_t  _tetraX = 2, _tetraY = 0;
  float   _tetraFall = 0, _tetraPause = 0;
  uint16_t _tetraScore = 0;
  uint32_t _tetraRng = 0x91E10DA5u;
  bool _tetrisStarted = false;
  bool _tetraLost = false;

  void resetRun();
  void step(float dt);
  void render();
  void drawHero(int cx, int cy);
  void drawWisp(int idx, int cx, int cy);
  void resetTetris();
  void stepTetris(float dt);
  void renderTetris();
  bool tetraCollides(int kind, int rot, int x, int y) const;
  void spawnTetra();
  void lockTetra();
  uint32_t nextTetraRandom();
};
