#pragma once
// Shared "smooth font" support: a Helvetica-like GFXfont (FreeSansBold, in one of four sizes) used
// in place of the blown-up classic 5x7 bitmap font for a module's one big number, plus the fitting
// logic that picks the largest size that still fits its box. Modules pair this with
// Canvas::antialiasText() (see Display.h) right after printing, to soften the font's stair-stepped
// corners at these small pixel sizes.
//
// Usage pattern (see Countdown.cpp / ClockModule.cpp / Weather.cpp / Gas.cpp for the full versions):
//   SmoothFit fit = fitSmoothFont(s, maxW, maxH);
//   if (fit.font) {
//     int y = desiredTop - fit.top, x = desiredLeft - fit.xBearing;
//     gDisplay->setCursor(x, y); gDisplay->print(s);
//     gDisplay->antialiasText(desiredLeft, desiredTop, fit.w, fit.h, color);
//   } else {
//     // fall back to the classic bitmap font -- nothing fit the box
//   }
//   gDisplay->setFont(nullptr);

#include <Adafruit_GFX.h>

// Largest first: the first one whose actual rendered size fits the caller's box wins.
// A real (not extern) compile-time constant, so callers can size a local array with it.
enum { SMOOTH_FONT_COUNT = 4 };
extern const GFXfont *const SMOOTH_FONTS[SMOOTH_FONT_COUNT];

struct SmoothFit {
  const GFXfont *font;      // nullptr if nothing fit; caller falls back to the classic bitmap font
  int16_t        xBearing;
  int16_t        top;
  uint16_t       w, h;
};

// Tries each font in SMOOTH_FONTS, largest first, and returns the first whose ink (as measured by
// getTextBounds at x=0,y=0) fits within maxW x maxH. Leaves that font set on gDisplay (or, if none
// fit, resets it to the classic font) -- callers still need to setFont(nullptr) once done drawing.
SmoothFit fitSmoothFont(const char *s, int maxW, int maxH);
