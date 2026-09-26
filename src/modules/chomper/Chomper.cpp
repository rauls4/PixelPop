#include "Chomper.h"
#include <math.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include <Fonts/TomThumb.h>            // tiny 3x5 font for the score

static const unsigned long FRAME_MS = 40;         // 25 frames per second

static const float BASE_SPEED   = 16.0f;          // pixels per second, the chomper running right
static const float WISP_CHASE  = 0.94f;          // wisps are a little slower: they never catch it
static const float HUNT_SPEED   = 1.25f;          // the chomper is faster while the ghosts are blue
static const float WISP_FRIGHT = 0.30f;          // blue ghosts crawl away
static const float EYES_SPEED   = 2.5f;           // eyes hurry back
static const float FREEZE_S     = 0.45f;          // brief stop when something is eaten

static const uint32_t WISP_COL[4] = {0xA040FF, 0x20D0B0, 0xFFC020, 0xFF40A0};   // violet, teal, gold, magenta

// ---------- settings ----------
void ChomperModule::onLoad() {
  _speed = store.getU8("spd", 1);
  if (_speed > 2) _speed = 1;
  _showScore = store.getBool("scr", true);
  _showLives = store.getBool("liv", true);
  _colWall   = store.getUInt("cWall", 0x2030FF);
}

String ChomperModule::onSettingsHtml() {
  String h;
  h += uiSection("Show");
  static const char *const speeds[] = {"Slow", "Normal", "Fast"};
  h += uiSelect("spd", "Speed", speeds, 3, _speed);
  h += uiCheckbox("scr", "Show the score", _showScore);
  h += uiCheckbox("liv", "Show the spare lives", _showLives);
  h += uiSection("Colors");
  h += uiColor("cWall", "Maze walls", _colWall);
  h += "<p class='m'>The whole story (chase, power pellet, eating the blue ghosts) takes about "
       "10 seconds at normal speed. Landscape waits for the full story to finish, then advances to "
       "the next module; it starts from the beginning when Arcade returns.</p>";
  return h;
}

void ChomperModule::onSave(WebServer &server) {
  _speed = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _showScore = server.hasArg("scr");
  _showLives = server.hasArg("liv");
  uint32_t c;
  if (uiReadColor(server, "cWall", c)) _colWall = c;
  store.putU8("spd", _speed);
  store.putBool("scr", _showScore);
  store.putBool("liv", _showLives);
  store.putUInt("cWall", _colWall);
}

String ChomperModule::summary() {
  static const char *const names[] = {"slow", "normal speed", "fast"};
  return String("Landscape chase; portrait self-playing blocks - ") + names[_speed];
}

// ---------- the show ----------
void ChomperModule::resetRun() {
  const bool portrait = gDisplay->height() > gDisplay->width();
  const float trackLength = (float)(portrait ? gDisplay->height() : gDisplay->width());
  _stage = CHASE;
  _px = -6;                                        // just off the left edge
  _dir = 1;
  const float gap[NWISP] = {14, 26, 38, 50};      // the first one is right behind him
  for (int i = 0; i < NWISP; i++) { _wx[i] = _px - gap[i]; _wm[i] = NORMAL; }
  _pelletX = trackLength - 5;
  _pellet = true;
  _nDots = 0;
  for (float x = 5; x <= _pelletX - 7 && _nDots < MAX_DOTS; x += 4) _dot[_nDots++] = true;
  _anim = 0; _clock = 0; _freeze = 0; _popWisp = -1; _pause = 0; _eaten = 0; _score = 0;
}

static void smallText(int x, int baseline, const char *t, uint16_t col);

static const uint16_t TETRA_MASK[7][4] = {
    {0x00F0, 0x2222, 0x00F0, 0x2222}, // I
    {0x0660, 0x0660, 0x0660, 0x0660}, // O
    {0x0270, 0x0262, 0x0072, 0x0232}, // T
    {0x0360, 0x0231, 0x0036, 0x0462}, // L
    {0x0630, 0x0132, 0x0063, 0x0264}, // J
    {0x0360, 0x0231, 0x0036, 0x0462}, // L-like fast variety
    {0x0360, 0x0231, 0x0036, 0x0462}  // another varied color/shape
};
static const uint32_t TETRA_COL[7] = {0x35C8FF, 0xFFD142, 0xB768FF, 0xFF8B3D, 0x4FE274, 0xFF4F8B, 0x4AD8C8};

uint32_t ChomperModule::nextTetraRandom() {
    _tetraRng ^= _tetraRng << 13;
    _tetraRng ^= _tetraRng >> 17;
    _tetraRng ^= _tetraRng << 5;
    return _tetraRng;
}

bool ChomperModule::tetraCollides(int kind, int rot, int x, int y) const {
    const uint16_t mask = TETRA_MASK[kind][rot & 3];
    for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++) {
      if (!(mask & (1U << (row * 4 + col)))) continue;
      const int bx = x + col, by = y + row;
      if (bx < 0 || bx >= TETRA_W || by >= TETRA_H) return true;
      if (by >= 0 && _tetra[by][bx]) return true;
    }
    return false;
}

void ChomperModule::resetTetris() {
    memset(_tetra, 0, sizeof(_tetra));
    _tetraScore = 0;
    _tetraFall = _tetraPause = 0;
    _tetraLost = false;
    _tetraRng ^= (uint32_t)millis();
    spawnTetra();
}

void ChomperModule::spawnTetra() {
    _tetraKind = nextTetraRandom() % 7;
    int best = 100000, bestX = 2, bestRot = 0;
    for (int rot = 0; rot < 4; rot++) for (int x = -1; x < TETRA_W; x++) {
      if (tetraCollides(_tetraKind, rot, x, 0)) continue;
      int y = 0;
      while (!tetraCollides(_tetraKind, rot, x, y + 1)) y++;
      int sum = 0, maxH = 0;
      const uint16_t mask = TETRA_MASK[_tetraKind][rot];
      for (int col = 0; col < TETRA_W; col++) {
        int top = TETRA_H;
        for (int row = 0; row < TETRA_H; row++) {
          bool occupied = _tetra[row][col];
          const int pr = row - y, pc = col - x;
          if (pr >= 0 && pr < 4 && pc >= 0 && pc < 4 && (mask & (1U << (pr * 4 + pc)))) occupied = true;
          if (occupied) { top = row; break; }
        }
        const int height = TETRA_H - top;
        sum += height; if (height > maxH) maxH = height;
      }
      const int score = sum + maxH * 3 + abs(x - 2);
      if (score < best) { best = score; bestX = x; bestRot = rot; }
    }
    _tetraRot = bestRot;
    _tetraX = bestX;
    _tetraY = 0;
    if (tetraCollides(_tetraKind, _tetraRot, _tetraX, _tetraY)) {
      _tetraLost = true;
      _tetraPause = 1.5f;
    }
}

void ChomperModule::lockTetra() {
    const uint16_t mask = TETRA_MASK[_tetraKind][_tetraRot];
    for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++)
      if (mask & (1U << (row * 4 + col))) {
        const int x = _tetraX + col, y = _tetraY + row;
        if (x >= 0 && x < TETRA_W && y >= 0 && y < TETRA_H) _tetra[y][x] = _tetraKind + 1;
      }
    int cleared = 0;
    for (int row = TETRA_H - 1; row >= 0; row--) {
      bool full = true;
      for (int col = 0; col < TETRA_W; col++) if (!_tetra[row][col]) { full = false; break; }
      if (!full) continue;
      for (int above = row; above > 0; above--) memcpy(_tetra[above], _tetra[above - 1], TETRA_W);
      memset(_tetra[0], 0, TETRA_W);
      cleared++;
      row++;
    }
    _tetraScore += cleared ? (cleared * cleared * 100) : 10;
    spawnTetra();
}

void ChomperModule::stepTetris(float dt) {
    if (_tetraLost) {
      _tetraPause -= dt;
      if (_tetraPause <= 0) resetTetris();
      return;
    }
    if (_tetraPause > 0) {
      _tetraPause -= dt;
      if (_tetraPause <= 0) resetTetris();
      return;
    }
    _tetraFall += dt * (_speed == 0 ? 0.85f : (_speed == 2 ? 1.75f : 1.25f));
    if (_tetraFall < 0.20f) return;
    _tetraFall = 0;
    if (!tetraCollides(_tetraKind, _tetraRot, _tetraX, _tetraY + 1)) _tetraY++;
    else lockTetra();
}

void ChomperModule::renderTetris() {
    const int W = gDisplay->width(), H = gDisplay->height();
    const int cell = 3, boardW = TETRA_W * cell, boardH = TETRA_H * cell;
    const int left = (W - boardW) / 2, top = (H - boardH) / 2;
    gDisplay->fillScreen(0);
    gDisplay->drawRect(left - 1, top - 1, boardW + 2, boardH + 2, rgb565(_colWall));
    for (int row = 0; row < TETRA_H; row++) for (int col = 0; col < TETRA_W; col++)
      if (_tetra[row][col]) gDisplay->fillRect(left + col * cell, top + row * cell, cell - 1, cell - 1, rgb565(TETRA_COL[_tetra[row][col] - 1]));
    if (!_tetraLost) {
      const uint16_t mask = TETRA_MASK[_tetraKind][_tetraRot];
      for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++)
        if (mask & (1U << (row * 4 + col))) {
          const int x = _tetraX + col, y = _tetraY + row;
          if (x >= 0 && x < TETRA_W && y >= 0 && y < TETRA_H)
            gDisplay->fillRect(left + x * cell, top + y * cell, cell - 1, cell - 1, rgb565(TETRA_COL[_tetraKind]));
        }
    }
    if (_tetraLost) {
      const char *message = "GAME OVER";
      smallText((W - (int)strlen(message) * 4) / 2, 6, message, COL_WHITE);
    }
    gDisplay->flipDMABuffer();
}

void ChomperModule::step(float dt) {
  _clock += dt;
  if (_freeze > 0) {                               // everything holds still for a moment
    _freeze -= dt;
    if (_freeze <= 0) { _freeze = 0; _popWisp = -1; }
    return;
  }
  _anim += dt;
  const float sp = (_speed == 0 ? 0.7f : (_speed == 2 ? 1.5f : 1.0f));
  const float v = BASE_SPEED * sp;

  switch (_stage) {
    case CHASE: {
      _px += v * dt;
      for (int i = 0; i < NWISP; i++) _wx[i] += v * WISP_CHASE * dt;
      int k = 0;
      for (float x = 5; k < _nDots; x += 4, k++)
        if (_dot[k] && x <= _px + 2) { _dot[k] = false; _score += 10; }
      if (_pellet && _px >= _pelletX - 4) {        // glowing star!
        _pellet = false;
        _score += 50;
        for (int i = 0; i < NWISP; i++) _wm[i] = FRIGHT;
        _dir = -1;
        _stage = HUNT;
        _eaten = 0;
        _freeze = FREEZE_S;
      }
      break;
    }
    case HUNT:
    case EXIT: {
      _px -= v * HUNT_SPEED * dt;
      for (int i = 0; i < NWISP; i++) {
        if (_wm[i] == FRIGHT)     _wx[i] -= v * WISP_FRIGHT * dt;
        else if (_wm[i] == EYES) { _wx[i] -= v * EYES_SPEED * dt; if (_wx[i] < -7) _wm[i] = GONE; }
      }
      if (_stage == HUNT) {
        int left = 0;
        for (int i = 0; i < NWISP; i++) {
          if (_wm[i] != FRIGHT) continue;
          if (fabsf(_px - _wx[i]) <= 6.0f) {       // caught one
            _wm[i] = EYES;
            int pts = 100 << _eaten;               // 100, 200, 400, 800
            _eaten++;
            _score += pts;
            snprintf(_popText, sizeof(_popText), "%d", pts);
            _popWisp = i; _popX = _wx[i];
            _freeze = FREEZE_S;
            break;
          }
        }
        for (int i = 0; i < NWISP; i++) if (_wm[i] == FRIGHT) left++;
        if (left == 0) _stage = EXIT;
      }
      if (_stage == EXIT && _px < -7) { _stage = PAUSE; _pause = 0.8f; }
      break;
    }
    case PAUSE:
      _pause -= dt;
      if (_pause <= 0) {
        _landscapeComplete = true;
        // Normally the rotation notices pageProgress() == 2 and moves on well within this window,
        // which calls pageEntered() -> resetRun() the next time it comes back around. But if this
        // is the only active module, the rotation never looks away, so nothing else will ever call
        // resetRun() for us - loop it ourselves rather than sit on the finished frame forever.
        if (_pause <= -1.5f) resetRun();
      }
      break;
  }
}

// ---------- drawing ----------
// The chomper: a round yellow disc, 11 x 11 (no eyes) with a pie-wedge mouth that opens and
// closes around the horizontal axis. Drawn facing right and mirrored for left.
void ChomperModule::drawHero(int cx, int cy) {
  const uint16_t yellow = rgb565(0xFFE000);
  const bool portrait = gDisplay->height() > gDisplay->width();
  // how wide the wedge is: slope of its edges (rows per column). Closed, small, wide, widest, wide, small.
  static const float slope[6] = {-1.0f, 0.35f, 0.8f, 1.3f, 0.8f, 0.35f};
  const float t = slope[((int)(_anim * 12.0f)) % 6];
  for (int dy = -5; dy <= 5; dy++) {
    for (int dx = -5; dx <= 5; dx++) {
      if (dx * dx + dy * dy > 29) continue;                    // round outline
      if (t >= 0 && dx >= 1 && fabsf((float)dy) <= t * dx) continue;     // the open mouth
      if (portrait) gDisplay->drawPixel(cx + dy, cy + dx * _dir, yellow);
      else          gDisplay->drawPixel(cx + dx * _dir, cy + dy, yellow);
    }
  }
}

// A wisp: an 11 x 11 ghost-like blob, round on top, with two eyes and a wavy hem.
void ChomperModule::drawWisp(int idx, int cx, int cy) {
  const WispMode mode = _wm[idx];
  if (mode == GONE) return;
  const bool portrait = gDisplay->height() > gDisplay->width();
  const bool facingRight = !(mode == EYES || _dir < 0 || _stage != CHASE);
  const auto pixel = [=](int x, int y, uint16_t color) {
    // A portrait corridor runs vertically. Keep the sprite upright when the
    // chase reverses; flipping it for the attack phase turns its head upside down.
    if (portrait) gDisplay->drawPixel(cx + x, cy + y, color);
    else          gDisplay->drawPixel(cx + x, cy + y, color);
  };

  if (mode != EYES) {
    const uint16_t col = (mode == FRIGHT) ? rgb565(0x2828FF) : rgb565(WISP_COL[idx]);
    static const uint16_t hemA = 0x5DD, hemB = 0x376;        // 1.111.111.1 / .11.111.11 style feet (11 bits)
    const bool alt = ((int)(_anim * 6.0f)) & 1;
    for (int r = 0; r < 11; r++) {
      for (int c = 0; c < 11; c++) {
        bool on;
        if (r < 6) { int dx = c - 5, dy = r - 5; on = dx * dx + dy * dy <= 29; }   // round head
        else if (r < 10) on = true;
        else on = ((alt ? hemB : hemA) >> (10 - c)) & 1;
        if (on) pixel(c - 5, r - 5, col);
      }
    }
  }

  const uint16_t white = (mode == FRIGHT) ? rgb565(0xFFD0B0) : COL_WHITE;
  const uint16_t pupil = rgb565(0x1848FF);                     // blue pupils
  if (mode == FRIGHT) {                                        // scared: two small pink eyes and a wobbly mouth
    for (int e = 0; e < 2; e++)
      for (int r = 3; r <= 4; r++) pixel((e ? 7 : 3) - 5, r - 5, white);
    for (int c = 2; c <= 8; c++) if (c & 1) pixel(c - 5, 2, white);
    for (int c = 3; c <= 7; c++) if (!(c & 1)) pixel(c - 5, 3, white);
  } else {                                                     // two big eyes, 3 x 4 white with a 2 x 2 blue pupil
    for (int e = 0; e < 2; e++) {
      const int ex = e ? 6 : 2;
      for (int r = 3; r < 7; r++)
        for (int c = ex; c < ex + 3; c++) pixel(c - 5, r - 5, white);
      for (int r = 4; r < 6; r++)
        for (int c = ex + (facingRight ? 1 : 0); c < ex + (facingRight ? 3 : 2); c++) pixel(c - 5, r - 5, pupil);
    }
  }
}

// A filled round dot: every pixel within the radius (given as radius squared) of the center.
static void disc(int cx, int cy, int half, int r2, uint16_t col) {
  for (int dy = -half; dy <= half; dy++)
    for (int dx = -half; dx <= half; dx++)
      if (dx * dx + dy * dy <= r2) gDisplay->drawPixel(cx + dx, cy + dy, col);
}

static void smallText(int x, int baseline, const char *t, uint16_t col) {
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(t);
  gDisplay->setFont(nullptr);
}

void ChomperModule::render() {
  const int W = gDisplay->width(), H = gDisplay->height();
  const bool portrait = H > W;
  const int trackCenter = portrait ? W / 2 : H / 2;
  const bool roomy = (H >= 28);          // room for the score and the spare lives
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);

  // maze walls: a single one-pixel line above and below the corridor
  const uint16_t wall = rgb565(_colWall);
  if (portrait) {
    gDisplay->drawFastVLine(trackCenter - 8, 0, H, wall);
    gDisplay->drawFastVLine(trackCenter + 8, 0, H, wall);
  } else {
    gDisplay->drawFastHLine(0, trackCenter - 8, W, wall);
    gDisplay->drawFastHLine(0, trackCenter + 8, W, wall);
  }

  // dots and the power pellet
  const uint16_t dotCol = rgb565(0xE8D8A0);
  int k = 0;
  for (float x = 5; k < _nDots; x += 4, k++)
    if (_dot[k]) {
      if (portrait) gDisplay->fillRect(trackCenter - 1, (int)x - 1, 2, 2, dotCol);
      else          gDisplay->fillRect((int)x - 1, trackCenter - 1, 2, 2, dotCol);
    }
  if (_pellet && (((int)(_clock * 4.0f)) & 1) == 0)            // glowing round power pellet
    disc(portrait ? trackCenter : (int)_pelletX, portrait ? (int)_pelletX : trackCenter, 3, 10, rgb565(0xFFE070));

  // score (tiny font, above the top wall) and spare lives (below the bottom wall)
  if (roomy && _showScore) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _score);
    smallText(1, portrait ? 6 : trackCenter - 10, buf, COL_WHITE); // glyphs fill rows 1-5
  }
  if (roomy && _showLives) {
    const uint16_t yellow = rgb565(0xFFE000);
    for (int i = 0; i < 3; i++)
      if (portrait) disc(W - 4, 8 + i * 7, 2, 5, yellow);
      else          disc(3 + i * 7, trackCenter + 12, 2, 5, yellow);
  }

  // wisps, then the chomper on top
  for (int i = 0; i < NWISP; i++) {
    if (i == _popWisp && _freeze > 0) continue;              // its score is shown instead
    drawWisp(i, portrait ? trackCenter : (int)lroundf(_wx[i]), portrait ? (int)lroundf(_wx[i]) : trackCenter);
  }
  if (!(_popWisp >= 0 && _freeze > 0) && _stage != PAUSE)
    drawHero(portrait ? trackCenter : (int)lroundf(_px), portrait ? (int)lroundf(_px) : trackCenter);

  // "200" / "400" / ... where a ghost was eaten
  if (_popWisp >= 0 && _freeze > 0) {
    int w = (int)strlen(_popText) * 4 - 1;
    int x = (int)lroundf(_popX) - w / 2;
    if (x < 0) x = 0;
    if (x + w > W) x = W - w;
    smallText(portrait ? (W - w) / 2 : x, portrait ? (int)lroundf(_popX) + 3 : trackCenter + 3, _popText, rgb565(0x00FFFF));
  }
  gDisplay->flipDMABuffer();
}

bool ChomperModule::needsRedraw() {
  return millis() - _lastFrame >= FRAME_MS;
}

int ChomperModule::pageProgress() {
  if (!gDisplay) return 0;
  if (gDisplay->height() <= gDisplay->width())
    return _landscapeComplete ? 2 : 1;
  return _tetrisStarted && !_tetraLost ? 1 : 0;
}

void ChomperModule::pageEntered(int sub) {
  (void)sub;
  if (gDisplay && gDisplay->height() <= gDisplay->width()) {
    resetRun();
    _started = true;
    _landscapeComplete = false;
    _lastFrame = millis();
    return;
  }
  if (gDisplay && gDisplay->height() > gDisplay->width() && _tetraLost) {
    resetTetris();
    _lastFrame = millis();
  }
}

void ChomperModule::drawPage(int sub) {
  (void)sub;
  unsigned long now = millis();
  if (gDisplay->height() > gDisplay->width()) {
    if (!_tetrisStarted) { resetTetris(); _tetrisStarted = true; _lastFrame = now; }
    float dt = (now - _lastFrame) / 1000.0f;
    if (dt > 0.06f) dt = 0.06f;
    _lastFrame = now;
    stepTetris(dt);
    renderTetris();
    return;
  }
  // first frame, or the page has just come around again: start the story over
  if (!_started || now - _lastFrame > 700) { resetRun(); _started = true; _lastFrame = now; }
  float dt = (now - _lastFrame) / 1000.0f;
  if (dt > 0.06f) dt = 0.06f;
  _lastFrame = now;
  step(dt);
  render();
}
