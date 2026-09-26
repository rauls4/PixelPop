#include "Logos.h"
#include "LogoData.h"
#include "../../core/Display.h"

// 3 characters (0-9, A-Z) -> one number; must match tools/make_logos.py
static bool packCode(const char *c, uint16_t &out) {
  uint32_t v = 0;
  for (int i = 0; i < 3; i++) {
    char ch = c[i];
    int d;
    if (ch >= '0' && ch <= '9') d = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') d = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 10;
    else return false;
    v = v * 36 + d;
  }
  out = (uint16_t)v;
  return true;
}

// index of the picture for this code, or -1
static int findImage(const char *code) {
  uint16_t key;
  if (!code || !packCode(code, key)) return -1;
  int lo = 0, hi = (int)LOGO_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t v = LOGO_CODES[mid];
    if (v == key) return LOGO_IMAGE[mid];
    if (v < key) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

int  logoCount() { return LOGO_COUNT; }
int  logoSize() { return LOGO_SIZE; }
bool logoAvailable(const char *code) { return findImage(code) >= 0; }

bool logoDraw(const char *code, int x, int y, uint8_t brightness) {
  int idx = findImage(code);
  if (idx < 0 || !gDisplay) return false;
  if (brightness < 1) brightness = 1;
  if (brightness > 100) brightness = 100;

  const uint8_t *p = LOGO_DATA + LOGO_OFFSET[idx];
  const uint8_t *end = LOGO_DATA + LOGO_OFFSET[idx + 1];
  int pos = 0;
  while (p + 3 <= end && pos < LOGO_SIZE * LOGO_SIZE) {
    uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
    int run = p[2];
    p += 3;
    if (brightness < 100) {
      int r = ((c >> 11) & 31) * brightness / 100;
      int g = ((c >> 5) & 63) * brightness / 100;
      int b = (c & 31) * brightness / 100;
      c = (uint16_t)((r << 11) | (g << 5) | b);
    }

    while (run-- > 0 && pos < LOGO_SIZE * LOGO_SIZE) {
      gDisplay->drawPixel(x + pos % LOGO_SIZE, y + pos / LOGO_SIZE, c);
      pos++;
    }
  }
  return true;
}

bool logoDrawScaled(const char *code, int x, int y, uint8_t scale, uint8_t brightness) {
  int idx = findImage(code);
  if (idx < 0 || !gDisplay || scale < 1) return false;
  if (brightness < 1) brightness = 1;
  if (brightness > 100) brightness = 100;

  const uint8_t *p = LOGO_DATA + LOGO_OFFSET[idx];
  const uint8_t *end = LOGO_DATA + LOGO_OFFSET[idx + 1];
  int pos = 0;
  while (p + 3 <= end && pos < LOGO_SIZE * LOGO_SIZE) {
    uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
    int run = p[2];
    p += 3;
    if (brightness < 100) {
      int r = ((c >> 11) & 31) * brightness / 100;
      int g = ((c >> 5) & 63) * brightness / 100;
      int b = (c & 31) * brightness / 100;
      c = (uint16_t)((r << 11) | (g << 5) | b);
    }
    while (run-- > 0 && pos < LOGO_SIZE * LOGO_SIZE) {
      gDisplay->fillRect(x + (pos % LOGO_SIZE) * scale, y + (pos / LOGO_SIZE) * scale, scale, scale, c);
      pos++;
    }
  }
  return true;
}

bool logoCodeFromCallsign(const char *cs, char out[4]) {
  if (!cs) return false;
  for (int i = 0; i < 3; i++) {
    if (!(cs[i] >= 'A' && cs[i] <= 'Z')) return false;
  }
  if (!(cs[3] >= '0' && cs[3] <= '9')) return false;
  out[0] = cs[0]; out[1] = cs[1]; out[2] = cs[2]; out[3] = 0;
  return true;
}
