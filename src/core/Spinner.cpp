#include "Spinner.h"
#include <math.h>
#include "Display.h"

// A deliberate, high-contrast eight-step orbit. Twelve small dots looked static
// on a 64x32 panel even though their brightness was advancing.
void displaySpinner(unsigned long nowMs) {
  static const int N = 8;
  static const uint8_t LEVEL[N] = {255, 136, 76, 42, 25, 16, 10, 7};   // head first
  const int W = gDisplay->width(), H = gDisplay->height();
  const int cx = W / 2, cy = H / 2;
  int R = H / 2 - 4;
  if (R > 11) R = 11;
  const int head = (int)((nowMs / 100) % N);
  const uint32_t base = 0x50B8FF;

  gDisplay->fillScreen(0);
  for (int i = 0; i < N; i++) {
    const int behind = (head - i + N) % N;                      // 0 = the head
    const uint32_t lv = LEVEL[behind];
    const uint32_t r = ((base >> 16) & 0xFF) * lv / 255, g = ((base >> 8) & 0xFF) * lv / 255, b = (base & 0xFF) * lv / 255;
    const float a = ((float)i / N) * 6.2831853f - 1.5707963f;   // start at the top, go clockwise
    const int x = cx + (int)lroundf(cosf(a) * R), y = cy + (int)lroundf(sinf(a) * R);
    const int size = behind == 0 ? 3 : 2;
    gDisplay->fillRect(x - size / 2, y - size / 2, size, size, rgb565((r << 16) | (g << 8) | b));
  }
  gDisplay->flipDMABuffer();
}
