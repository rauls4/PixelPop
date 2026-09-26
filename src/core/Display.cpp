#include "Display.h"
#include "SplashLogos.h"
#include <string.h>
#include <new>

Canvas *gDisplay = nullptr;
uint16_t COL_WHITE = 0, COL_YELLOW = 0, COL_GRAY = 0, COL_BLUE = 0, COL_ORANGE = 0;

// ---------- Canvas ----------
Canvas::Canvas(MatrixPanel_I2S_DMA *panel, bool doubleBuffered)
  : Adafruit_GFX(PANEL_W, PANEL_H), _panel(panel), _doubleBuffered(doubleBuffered) {
  memset(_shadow, 0, sizeof(_shadow));
  memset(_onPanel, 0, sizeof(_onPanel));
}

// Single-buffered: push only the pixels whose (mapped) color differs from what the panel
// shows. Also picks up color-order / gain changes, since those change the mapped value.
void Canvas::present() {
  if (!_live || _doubleBuffered) return;
  for (int y = 0; y < PANEL_H; y++) {
    for (int x = 0; x < PANEL_W; x++) {
      const int i = y * PANEL_W + x;
      const uint16_t m = mapColor(_shadow[i]);
      if (m != _onPanel[i]) { _panel->drawPixel(x, y, m); _onPanel[i] = m; }
    }
  }
}

void Canvas::flipDMABuffer() {
  if (!_live) return;
  if (_doubleBuffered) { _panel->flipDMABuffer(); return; }
  if (_rawFrame) { _rawFrame = false; return; }   // a transition frame is already on the panel
  present();
}

void Canvas::rawPixel(int x, int y, uint16_t c) {
  const uint16_t m = mapColor(c);
  if (_doubleBuffered) { _panel->drawPixel(x, y, m); return; }
  const int i = y * PANEL_W + x;
  if (m != _onPanel[i]) { _panel->drawPixel(x, y, m); _onPanel[i] = m; }
  _rawFrame = true;
}

void Canvas::setPadding(uint8_t pixels) {
  if (pixels > MAX_PADDING) pixels = MAX_PADDING;
  _pad = pixels;
  WIDTH = _width = (portrait() ? PANEL_H : PANEL_W) - 2 * pixels;
  HEIGHT = _height = (portrait() ? PANEL_W : PANEL_H) - 2 * pixels;
}

void Canvas::setRotation(uint8_t rotation) {
  // Board-specific physical mounting, calibrated from the QMI8658 readings:
  // 0: landscape/bottom down, 90: portrait/bottom left,
  // 180: landscape/bottom up, 270: portrait/bottom right.
  static const uint8_t PANEL_ROTATION[] = {2, 3, 0, 1};
  _rotation = PANEL_ROTATION[rotation & 3];
  setPadding(_pad);
}

uint8_t Canvas::rotation() const {
  static const uint8_t LOGICAL_ROTATION[] = {2, 3, 0, 1};
  return LOGICAL_ROTATION[_rotation & 3];
}

static const char *const ORDER_NAMES[COLOR_ORDERS] = {
  "Normal (red, green, blue)", "Swap green and blue", "Swap red and green",
  "Swap red and blue", "Rotate: red > green > blue", "Rotate: red > blue > green"
};
const char *colorOrderName(int o) { return (o >= 0 && o < COLOR_ORDERS) ? ORDER_NAMES[o] : ORDER_NAMES[0]; }

void Canvas::setColorOrder(uint8_t order) { _order = (order < COLOR_ORDERS) ? order : 0; }

void Canvas::setGain(uint8_t r, uint8_t g, uint8_t b) { _gainR = r; _gainG = g; _gainB = b; }

uint16_t Canvas::mapColor(uint16_t c) const {
  if (c == 0) return 0;
  if (_order == 0 && _gainR == 255 && _gainG == 255 && _gainB == 255) return c;    // nothing to change
  int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
  int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);   // 8 bits each
  if (_gainR != 255) r = (r * _gainR) / 255;
  if (_gainG != 255) g = (g * _gainG) / 255;
  if (_gainB != 255) b = (b * _gainB) / 255;
  int o0, o1, o2;
  switch (_order) {
    case 1:  o0 = r; o1 = b; o2 = g; break;      // swap green and blue
    case 2:  o0 = g; o1 = r; o2 = b; break;      // swap red and green
    case 3:  o0 = b; o1 = g; o2 = r; break;      // swap red and blue
    case 4:  o0 = b; o1 = r; o2 = g; break;      // rotate
    case 5:  o0 = g; o1 = b; o2 = r; break;      // rotate the other way
    default: o0 = r; o1 = g; o2 = b; break;      // normal
  }
  return (uint16_t)(((o0 >> 3) << 11) | ((o1 >> 2) << 5) | (o2 >> 3));
}

// Every drawing call updates the shadow copy, and the panel unless capturing.
void Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= _width || y >= _height) return;
  if (x < _clipL || x >= _clipR) return;
  x += _pad; y += _pad;
  int panelX = x, panelY = y;
  switch (_rotation) {
    case 1: panelX = y; panelY = PANEL_H - 1 - x; break;
    case 2: panelX = PANEL_W - 1 - x; panelY = PANEL_H - 1 - y; break;
    case 3: panelX = PANEL_W - 1 - y; panelY = x; break;
  }
  _shadow[panelY * PANEL_W + panelX] = color;
  if (_live && _doubleBuffered) _panel->drawPixel(panelX, panelY, mapColor(color));
}

// Same canvas -> panel transform as drawPixel(), read instead of written.
uint16_t Canvas::getPixel(int16_t x, int16_t y) const {
  if (x < 0 || y < 0 || x >= _width || y >= _height) return 0;
  x += _pad; y += _pad;
  int panelX = x, panelY = y;
  switch (_rotation) {
    case 1: panelX = y; panelY = PANEL_H - 1 - x; break;
    case 2: panelX = PANEL_W - 1 - x; panelY = PANEL_H - 1 - y; break;
    case 3: panelX = PANEL_W - 1 - y; panelY = x; break;
  }
  return _shadow[panelY * PANEL_W + panelX];
}

// Blends two rgb565 colors, pct 0..100 (0 = a, 100 = b).
static uint16_t lerp565(uint16_t a, uint16_t b, int pct) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (br - ar) * pct / 100;
  int g = ag + (bg - ag) * pct / 100;
  int bl = ab + (bb - ab) * pct / 100;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void Canvas::antialiasText(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t fg, uint16_t bg) {
  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > _width)  w = _width - x0;
  if (y0 + h > _height) h = _height - y0;
  if (w <= 0 || h <= 0) return;

  static bool on[PANEL_W * PANEL_H];               // scratch: big enough for the whole panel
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      on[j * w + i] = (getPixel(x0 + i, y0 + j) == fg);

  auto isOn = [&](int i, int j) {
    return i >= 0 && j >= 0 && i < w && j < h && on[j * w + i];
  };

  static const int WEIGHT[5] = {0, 0, 45, 65, 80};  // % blend toward fg, by foreground-neighbor count
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      if (on[j * w + i]) continue;                  // only soften background pixels
      int n = isOn(i - 1, j) + isOn(i + 1, j) + isOn(i, j - 1) + isOn(i, j + 1);
      if (WEIGHT[n] == 0) continue;                 // 0 or 1 neighbor: a flat edge, leave it crisp
      drawPixel(x0 + i, y0 + j, lerp565(bg, fg, WEIGHT[n]));
    }
  }
}

void Canvas::fillScreen(uint16_t color) {
  memset(_shadow, 0, sizeof(_shadow));         // the whole panel, padding included
  if (_live && _doubleBuffered) _panel->fillScreen(0);
  if (color) fillRect(0, 0, _width, _height, color);
}

void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > _width)  w = _width - x;
  if (y + h > _height) h = _height - y;
  if (w <= 0 || h <= 0) return;
  if (_rotation != 0) {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) drawPixel(i, j, color);
    return;
  }
  x += _pad; y += _pad;
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) _shadow[j * PANEL_W + i] = color;
  if (_live && _doubleBuffered) _panel->fillRect(x, y, w, h, mapColor(color));
}

void Canvas::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (y < 0 || y >= _height) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > _width) w = _width - x;
  if (w <= 0) return;
  if (_rotation != 0) {
    for (int i = x; i < x + w; i++) drawPixel(i, y, color);
    return;
  }
  x += _pad; y += _pad;
  for (int i = x; i < x + w; i++) _shadow[y * PANEL_W + i] = color;
  if (_live && _doubleBuffered) _panel->drawFastHLine(x, y, w, mapColor(color));
}

void Canvas::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  if (x < 0 || x >= _width) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > _height) h = _height - y;
  if (h <= 0) return;
  if (_rotation != 0) {
    for (int j = y; j < y + h; j++) drawPixel(x, j, color);
    return;
  }
  x += _pad; y += _pad;
  for (int j = y; j < y + h; j++) _shadow[j * PANEL_W + x] = color;
  if (_live && _doubleBuffered) _panel->drawFastVLine(x, y, h, mapColor(color));
}

// ---------- panel setup ----------
static MatrixPanel_I2S_DMA *gPanel = nullptr;

void displayBegin(uint8_t brightness, uint8_t padding) {
  HUB75_I2S_CFG mxconfig(PANEL_W, PANEL_H, 1);
  mxconfig.double_buff = false;
  // If the display looks garbled, try:
  // mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;

  gPanel = new (std::nothrow) MatrixPanel_I2S_DMA(mxconfig);
  if (!gPanel) {
    Serial.println("FATAL: could not allocate the LED panel driver");
    while (true) delay(1000);
  }
  if (!gPanel->begin()) {
    Serial.println("FATAL: could not initialize the LED panel driver");
    while (true) delay(1000);
  }
  gPanel->setBrightness8(0);
  gPanel->clearScreen();

  gDisplay = new (std::nothrow) Canvas(gPanel, mxconfig.double_buff);
  if (!gDisplay) {
    Serial.println("FATAL: could not allocate the display canvas");
    while (true) delay(1000);
  }
  gDisplay->setPadding(padding);
  gPanel->setBrightness8(brightness);

  COL_WHITE  = gDisplay->color565(255, 255, 255);
  COL_YELLOW = gDisplay->color565(255, 210, 0);
  COL_GRAY   = gDisplay->color565(150, 160, 175);
  COL_BLUE   = gDisplay->color565(60, 130, 255);
  COL_ORANGE = gDisplay->color565(255, 120, 0);
}

void displaySetBrightness(uint8_t brightness) {
  if (gDisplay) gDisplay->setBrightness8(brightness);
}

void displaySetPadding(uint8_t padding) {
  if (gDisplay) gDisplay->setPadding(padding);
}

void displaySetColorOrder(uint8_t order) {
  if (gDisplay) gDisplay->setColorOrder(order);
}

void displaySetGain(uint8_t r, uint8_t g, uint8_t b) {
  if (gDisplay) gDisplay->setGain(r, g, b);
}

void displaySetRotation(uint8_t rotation) {
  if (gDisplay) gDisplay->setRotation(rotation);
}

void displayBlackout() {
  if (!gDisplay) return;
  gDisplay->fillScreen(0);
  gDisplay->flipDMABuffer();
  gDisplay->fillScreen(0);
  gDisplay->flipDMABuffer();
  gDisplay->setBrightness8(0);
}

static void drawSplashContent() {
  const int W = gDisplay->width(), H = gDisplay->height();
  gDisplay->fillScreen(0);
  if (H > W) {
    gDisplay->drawBitmap((W - 32) / 2, (H - 28) / 2, SPLASH_LOGO_PORTRAIT, 32, 28, COL_WHITE);
  } else {
    gDisplay->drawBitmap((W - 64) / 2, (H - 22) / 2, SPLASH_LOGO_LANDSCAPE, 64, 22, COL_WHITE);
  }
}

void displayShowSplash() {
  if (!gDisplay) return;
  const int W = gDisplay->width(), H = gDisplay->height();
  static const uint32_t dust[] = {0xFFFFFF, 0xB8B8B8, 0x707070};

  // The splash is drawn in full, then revealed from left to right under a trail
  // of bright, drifting pixels.
  for (int edge = -8, frame = 0; edge < W + 10; edge += 3, frame++) {
    drawSplashContent();
    const int hiddenFrom = edge + 5;
    if (hiddenFrom < W) gDisplay->fillRect(hiddenFrom < 0 ? 0 : hiddenFrom, 0, W - (hiddenFrom < 0 ? 0 : hiddenFrom), H, 0);

    uint32_t seed = 0xC0FFEEu + (uint32_t)frame * 0x9E3779B9u;
    for (int i = 0; i < 18; i++) {
      seed = seed * 1664525u + 1013904223u;
      const int x = edge - 8 + (int)(seed % 15);
      seed = seed * 1664525u + 1013904223u;
      const int y = (int)(seed % H);
      if (x >= 0 && x < W) {
        const uint16_t color = rgb565(dust[(seed >> 8) % 3]);
        gDisplay->drawPixel(x, y, color);
        if ((seed & 3) == 0 && y + 1 < H) gDisplay->drawPixel(x, y + 1, color);
      }
    }
    gDisplay->flipDMABuffer();
    delay(28);
  }
  drawSplashContent();
  gDisplay->flipDMABuffer();
  delay(5000);
}

uint16_t rgb565(uint32_t c) {
  return gDisplay->color565((c >> 16) & 255, (c >> 8) & 255, c & 255);
}

void drawLines(const char *l1, const char *l2, const char *l3) {
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  gDisplay->setTextColor(COL_ORANGE);
  int y = l3 ? 2 : (l2 ? 8 : 12);
  gDisplay->setCursor(2, y);
  gDisplay->print(l1);
  if (l2) { gDisplay->setCursor(2, y + 10); gDisplay->print(l2); }
  if (l3) { gDisplay->setCursor(2, y + 20); gDisplay->print(l3); }
  gDisplay->flipDMABuffer();
}

// Named test colors for the Color Lab (Advanced > Color Lab): primaries, secondaries,
// white and a mid gray, enough to catch a swapped or unbalanced channel by name rather
// than by eye alone.
static const CalSwatch SWATCHES[CAL_SWATCH_COUNT] = {
  { "Red",     255,   0,   0 },
  { "Orange",  255, 128,   0 },
  { "Yellow",  255, 255,   0 },
  { "Green",     0, 255,   0 },
  { "Cyan",      0, 255, 255 },
  { "Blue",      0,   0, 255 },
  { "Purple",  128,   0, 255 },
  { "Magenta", 255,   0, 255 },
  { "White",   255, 255, 255 },
  { "Gray",    128, 128, 128 },
};
const CalSwatch *calSwatches() { return SWATCHES; }

void drawColorSwatch(int index) {
  if (!gDisplay) return;
  if (index < 0 || index >= CAL_SWATCH_COUNT) {
    gDisplay->fillScreen(0);
    gDisplay->flipDMABuffer();
    return;
  }
  const CalSwatch &sw = SWATCHES[index];
  gDisplay->fillScreen(gDisplay->color565(sw.r, sw.g, sw.b));

  // A black backing strip behind white text reads on every swatch color, including white itself.
  const int W = gDisplay->width(), H = gDisplay->height();
  const int textW = (int)strlen(sw.name) * 6;
  const int bw = (W - 2 < textW + 4) ? (W - 2) : (textW + 4);
  const int by = H - 9;
  gDisplay->fillRect(1, by, bw, 8, 0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  gDisplay->setTextColor(gDisplay->color565(255, 255, 255));
  gDisplay->setCursor(3, by + 1);
  gDisplay->print(sw.name);
  gDisplay->flipDMABuffer();
}
