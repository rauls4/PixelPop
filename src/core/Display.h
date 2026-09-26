#pragma once
// The LED panel: creation, brightness, shared colors, and tiny helpers that
// every module uses.
//
// gDisplay is a "canvas" on top of the panel. It works like the panel itself
// (text, lines, circles ...) but leaves a padding of unused pixels around the
// whole screen: drawing is shifted inward and clipped, and width()/height()
// report the usable size. Modules should use gDisplay->width() / height() rather
// than the raw PANEL_W / PANEL_H when they anchor things to the right or bottom.

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include "Config.h"

class Canvas : public Adafruit_GFX {
 public:
  explicit Canvas(MatrixPanel_I2S_DMA *panel, bool doubleBuffered);

  void    setPadding(uint8_t pixels);          // 0..MAX_PADDING
  uint8_t padding() const { return _pad; }
  void    setRotation(uint8_t rotation);       // physical orientation: 0, 90, 180, or 270 degrees clockwise
  uint8_t rotation() const;
  bool    portrait() const { return (_rotation & 1) != 0; }

  // Optional horizontal clip for single pixels and text: only columns x0 <= x < x1
  // (canvas coordinates) are drawn. Used for scrolling text.
  void setClipX(int x0, int x1) { _clipL = x0; _clipR = x1; }
  void clearClip() { _clipL = -32768; _clipR = 32767; }

  // Some panels wire their red / green / blue inputs in a different order, so a
  // color shows up as another one (blue looks green). This re-orders the color
  // channels on the way to the panel; everything drawn keeps its normal colors.
  void    setColorOrder(uint8_t order);        // 0 normal, see colorOrderName()
  uint8_t colorOrder() const { return _order; }

  // Per-channel intensity (0..255, 255 = full), applied before the color order above.
  // Some panels show one channel brighter or dimmer than the others (a color cast) even
  // once the wiring order is right; this balances that out.
  void    setGain(uint8_t r, uint8_t g, uint8_t b);
  void    gain(uint8_t &r, uint8_t &g, uint8_t &b) const { r = _gainR; g = _gainG; b = _gainB; }

  // drawing primitives (everything else in Adafruit_GFX builds on these)
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;    // also blanks the padding
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;

  // Screen transitions: while "capturing", drawing only goes into the shadow copy
  // (nothing reaches the panel and flipDMABuffer does nothing), so a finished
  // picture can be held and blended with the one on screen.
  void            beginCapture() { _live = false; }
  void            endCapture() { _live = true; }
  const uint16_t *shadow() const { return _shadow; }        // what is on screen, PANEL_W x PANEL_H
  void            rawPixel(int x, int y, uint16_t c);        // panel coordinates, straight to the panel (transitions)
  uint16_t        getPixel(int16_t x, int16_t y) const;      // canvas coordinates; what drawPixel() last put there

  // Softens the stair-stepped corners of text (or any solid shape) already drawn in fg on a bg
  // background: a background pixel next to two or more foreground pixels reads as a corner notch
  // and gets partially filled toward fg; a pixel next to only one (a flat edge) is left crisp.
  // Call it right after printing, with the same box, foreground and background colors used to draw it.
  void            antialiasText(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t fg, uint16_t bg = 0);

  // Single-buffered panels: drawing only goes into the shadow copy, and present() sends
  // just the pixels that changed since the last frame. Nothing is ever cleared on the live
  // panel first, so parts of the screen that did not change cannot flicker while something
  // else (scrolling text, ...) animates. flipDMABuffer() calls it at the end of every frame.
  void     present();

  // pass-throughs to the panel
  void     flipDMABuffer();
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return _panel->color565(r, g, b); }
  void     setBrightness8(uint8_t b) { _panel->setBrightness8(b); }

 private:
  MatrixPanel_I2S_DMA *_panel;
  uint8_t _pad = 0;
  int     _clipL = -32768, _clipR = 32767;
  bool    _live = true;
  bool    _doubleBuffered = false;
  uint8_t _rotation = 0;
  uint8_t _order = 0;
  uint8_t _gainR = 255, _gainG = 255, _gainB = 255;
  uint16_t mapColor(uint16_t c) const;          // apply the color order and gain
  uint16_t _shadow[PANEL_W * PANEL_H];
  uint16_t _onPanel[PANEL_W * PANEL_H];         // single-buffered: panel color (after mapColor) actually on the panel
  bool     _rawFrame = false;                   // a transition frame was drawn with rawPixel: flip must not overwrite it
};

#define MAX_PADDING 8

#define COLOR_ORDERS 6
const char *colorOrderName(int order);          // for the settings menu

// Named test colors for the Color Lab live calibration page (Advanced > Color Lab).
struct CalSwatch { const char *name; uint8_t r, g, b; };
#define CAL_SWATCH_COUNT 10
const CalSwatch *calSwatches();                 // CAL_SWATCH_COUNT entries
void drawColorSwatch(int index);                // fills the panel with calSwatches()[index] and labels it; -1 blanks it

extern Canvas *gDisplay;

// Fixed palette (used for icons and system messages)
extern uint16_t COL_WHITE, COL_YELLOW, COL_GRAY, COL_BLUE, COL_ORANGE;

void     displayBegin(uint8_t brightness, uint8_t padding);
void     displaySetBrightness(uint8_t brightness);
void     displaySetPadding(uint8_t padding);
void     displaySetColorOrder(uint8_t order);
void     displaySetGain(uint8_t r, uint8_t g, uint8_t b);
void     displaySetRotation(uint8_t rotation);
void     displayBlackout();             // clear both DMA buffers and darken the panel before a restart
void     displayShowSplash();
uint16_t rgb565(uint32_t rrggbb);                 // 0xRRGGBB -> panel color

// Up to three lines of small orange text (system messages)
void drawLines(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr);
