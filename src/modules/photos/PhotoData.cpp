#include "PhotoData.h"
#include <math.h>
#include <string.h>

static inline uint16_t pack565(int r, int g, int b) {
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

PhotoRect photoPlacement(int sw, int sh, int dw, int dh, int mode) {
  if (mode == FIT_BARS && sw > 0 && sh > 0) {
    float s = fminf((float)dw / sw, (float)dh / sh);
    int w = (int)lroundf(sw * s), h = (int)lroundf(sh * s);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > dw) w = dw;
    if (h > dh) h = dh;
    return {(dw - w) / 2, (dh - h) / 2, w, h};
  }
  return {0, 0, dw, dh};                       // scale and crop cover the whole screen
}

void photoRender(const uint16_t *src, int sw, int sh, uint16_t *dst, int dw, int dh, int mode, int brightPct) {
  memset(dst, 0, (size_t)dw * dh * sizeof(uint16_t));
  if (!src || sw < 1 || sh < 1 || dw < 1 || dh < 1) return;
  if (brightPct < 10) brightPct = 10;
  if (brightPct > 100) brightPct = 100;

  const PhotoRect p = photoPlacement(sw, sh, dw, dh, mode);

  // the part of the source picture that is shown (all of it, unless cropping)
  float wx = 0, wy = 0, ww = (float)sw, wh = (float)sh;
  if (mode == FIT_CROP) {
    float s = fmaxf((float)dw / sw, (float)dh / sh);
    ww = dw / s;
    wh = dh / s;
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;
  }

  for (int py = 0; py < p.h; py++) {
    float y0 = wy + py * wh / p.h, y1 = wy + (py + 1) * wh / p.h;
    int iy0 = (int)floorf(y0), iy1 = (int)ceilf(y1);
    if (iy0 < 0) iy0 = 0;
    if (iy1 > sh) iy1 = sh;
    for (int px = 0; px < p.w; px++) {
      float x0 = wx + px * ww / p.w, x1 = wx + (px + 1) * ww / p.w;
      int ix0 = (int)floorf(x0), ix1 = (int)ceilf(x1);
      if (ix0 < 0) ix0 = 0;
      if (ix1 > sw) ix1 = sw;

      // average of the source pixels under this screen pixel, weighted by how much of each is covered
      float r = 0, g = 0, b = 0, wsum = 0;
      for (int iy = iy0; iy < iy1; iy++) {
        float wyv = fminf((float)iy + 1, y1) - fmaxf((float)iy, y0);
        if (wyv <= 0) continue;
        for (int ix = ix0; ix < ix1; ix++) {
          float wxv = fminf((float)ix + 1, x1) - fmaxf((float)ix, x0);
          if (wxv <= 0) continue;
          float wgt = wxv * wyv;
          uint16_t c = src[iy * sw + ix];
          int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
          r += wgt * ((r5 << 3) | (r5 >> 2));
          g += wgt * ((g6 << 2) | (g6 >> 4));
          b += wgt * ((b5 << 3) | (b5 >> 2));
          wsum += wgt;
        }
      }
      if (wsum <= 0) continue;
      float k = brightPct / 100.0f / wsum;
      dst[(p.y + py) * dw + (p.x + px)] = pack565((int)(r * k + 0.5f), (int)(g * k + 0.5f), (int)(b * k + 0.5f));
    }
  }
}
