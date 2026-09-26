#include "SmoothFont.h"
#include "Display.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

const GFXfont *const SMOOTH_FONTS[SMOOTH_FONT_COUNT] = {
  &FreeSansBold24pt7b, &FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b
};

// getTextBounds() reports the same ink-relative box for any font (custom fonts are baseline-
// relative, the classic font is top-relative; either way "top" is where the ink actually starts
// once drawn at y). Calling it with x=0,y=0 gives bearing/height figures that translate cleanly:
// to place the ink's top at some row T, draw at y = T - top; to place its left edge at column L,
// draw at x = L - xBearing.
SmoothFit fitSmoothFont(const char *s, int maxW, int maxH) {
  gDisplay->setTextSize(1);          // custom fonts are still scaled by the classic font's size multiplier
  for (int i = 0; i < SMOOTH_FONT_COUNT; i++) {
    gDisplay->setFont(SMOOTH_FONTS[i]);
    int16_t x, top;
    uint16_t w, h;
    gDisplay->getTextBounds(s, 0, 0, &x, &top, &w, &h);
    if ((int)w <= maxW && (int)h <= maxH) return {SMOOTH_FONTS[i], x, top, w, h};
  }
  gDisplay->setFont(nullptr);
  return {nullptr, 0, 0, 0, 0};
}
