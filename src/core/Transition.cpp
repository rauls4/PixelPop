#include "Transition.h"
#include <math.h>
#include <string.h>
#include "Config.h"
#include "Display.h"
#include "WebUI.h"

enum {
  T_NONE, T_FADE, T_WIPE, T_LEFT, T_RIGHT, T_UP, T_DOWN, T_DISSOLVE, T_BLINDS, T_CIRCLE, T_RANDOM, T_COUNT
};

static const char *const NAMES[T_COUNT] = {
  "None (instant)", "Crossfade", "Wipe", "Slide left", "Slide right", "Slide up", "Slide down",
  "Dissolve", "Blinds", "Circle", "Random"
};
static const char *const SPEEDS[3] = {"Fast", "Normal", "Slow"};
static const unsigned long DURATION_MS[3] = {300, 500, 800};

int         transitionCount() { return T_COUNT; }
const char *transitionName(int s) { return (s >= 0 && s < T_COUNT) ? NAMES[s] : NAMES[0]; }
const char *transitionSpeedName(int s) { return (s >= 0 && s < 3) ? SPEEDS[s] : SPEEDS[1]; }

static uint16_t sFrom[PANEL_W * PANEL_H];          // the picture that was on screen

// 0..255, fixed pseudo-random value per pixel (for the dissolve)
static uint8_t hash8(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (uint8_t)h;
}

static uint16_t blend565(uint16_t a, uint16_t b, int alpha) {       // alpha 0..256 = amount of b
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (br - ar) * alpha / 256;
  int g = ag + (bg - ag) * alpha / 256;
  int bl = ab + (bb - ab) * alpha / 256;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// One frame of the effect. t runs 0 (old picture) to 1 (new picture).
static void drawFrame(int style, float t, const uint16_t *from, const uint16_t *to) {
  const int W = PANEL_W, H = PANEL_H;
  const float te = t * t * (3.0f - 2.0f * t);                       // ease in and out
  const int alpha = (int)(te * 256.0f + 0.5f);
  const int offX = (int)(te * W + 0.5f), offY = (int)(te * H + 0.5f);
  const int edge = (int)(te * W + 0.5f);
  const int thr = (int)(t * 256.0f + 0.5f);
  const int blindW = (int)(te * 8.0f + 0.5f);
  const float maxR = sqrtf((W / 2.0f) * (W / 2.0f) + (H / 2.0f) * (H / 2.0f)) + 1.0f;
  const float r2 = (te * maxR) * (te * maxR);

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int i = y * W + x;
      uint16_t c;
      switch (style) {
        case T_FADE:    c = blend565(from[i], to[i], alpha); break;
        case T_WIPE:    c = (x < edge) ? to[i] : from[i]; break;
        case T_LEFT: {  int sx = x + offX; c = (sx < W) ? from[y * W + sx] : to[y * W + sx - W]; break; }
        case T_RIGHT: { int sx = x - offX; c = (sx >= 0) ? from[y * W + sx] : to[y * W + sx + W]; break; }
        case T_UP: {    int sy = y + offY; c = (sy < H) ? from[sy * W + x] : to[(sy - H) * W + x]; break; }
        case T_DOWN: {  int sy = y - offY; c = (sy >= 0) ? from[sy * W + x] : to[(sy + H) * W + x]; break; }
        case T_DISSOLVE: c = (hash8(x, y) < thr) ? to[i] : from[i]; break;
        case T_BLINDS:  c = ((x & 7) < blindW) ? to[i] : from[i]; break;
        case T_CIRCLE: {
          float dx = x + 0.5f - W / 2.0f, dy = y + 0.5f - H / 2.0f;
          c = (dx * dx + dy * dy < r2) ? to[i] : from[i];
          break;
        }
        default:        c = to[i]; break;
      }
      gDisplay->rawPixel(x, y, c);
    }
  }
}

static uint32_t sRng = 2463534242u;

void transitionPlay(uint8_t style, uint8_t speed, Module *m, int sub) {
  if (!gDisplay || !m || style == T_NONE || style >= T_COUNT) return;
  if (speed > 2) speed = 1;

  if (style == T_RANDOM) {                          // any real effect
    sRng ^= sRng << 13; sRng ^= sRng >> 17; sRng ^= sRng << 5;
    sRng += millis();
    style = 1 + (sRng >> 8) % (T_RANDOM - 1);
  }

  // 1. keep the picture that is on screen
  memcpy(sFrom, gDisplay->shadow(), sizeof(sFrom));

  // 2. draw the new page "off screen"
  gDisplay->beginCapture();
  m->drawPage(sub);
  gDisplay->endCapture();
  // the shadow copy now holds the new picture

  // 3. blend, frame by frame (the web server keeps answering in between)
  const unsigned long dur = DURATION_MS[speed];
  const unsigned long t0 = millis();
  for (;;) {
    unsigned long e = millis() - t0;
    float t = (e >= dur) ? 1.0f : (float)e / (float)dur;
    drawFrame(style, t, sFrom, gDisplay->shadow());
    gDisplay->flipDMABuffer();
    if (t >= 1.0f) break;
    webLoop();
    delay(10);
  }
}
