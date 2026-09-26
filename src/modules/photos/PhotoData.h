#pragma once
// Picture fitting with no hardware in it (so it can be tested on a PC).
//
// A stored picture is small (at most PHOTO_MAX_SIDE pixels on its longest side, RGB565).
// photoRender() shrinks it onto the screen in one of three ways:
//   FIT_SCALE  stretch it to exactly the screen size (proportions may change)
//   FIT_CROP   fill the screen, keeping proportions, and cut off what does not fit
//   FIT_BARS   show the whole picture, keeping proportions, with black bars where needed
// Shrinking averages all the source pixels that land on a screen pixel, so photos stay smooth.

#include <stdint.h>

#define PHOTO_MAX_SIDE 128

enum PhotoFit { FIT_SCALE = 0, FIT_CROP = 1, FIT_BARS = 2 };

struct PhotoRect { int x, y, w, h; };

// Where the picture lands on a dw x dh screen (the part of the screen it covers).
PhotoRect photoPlacement(int sw, int sh, int dw, int dh, int mode);

// Draws the picture into dst (dw x dh, RGB565, row by row); everything the picture does not
// cover is black. brightPct scales the brightness (10..100).
void photoRender(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh, int mode, int brightPct);
