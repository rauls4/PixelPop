#include "WxIcons.h"
#include <math.h>
#include "Display.h"

const char *wxConditionText(int code) {
  if (code == 0)  return "Clear";
  if (code == 1)  return "Mostly clr";
  if (code == 2)  return "Pt cloudy";
  if (code == 3)  return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow shwr";
  if (code >= 95) return "Storm";
  return "---";
}

void drawWxDegree(int x, int y, uint16_t color) {
  gDisplay->drawFastHLine(x, y, 3, color);
  gDisplay->drawFastHLine(x, y + 2, 3, color);
  gDisplay->drawPixel(x, y + 1, color);
  gDisplay->drawPixel(x + 2, y + 1, color);
}

// ---------------- big icons ----------------
static void sunBig(int cx, int cy, int r) {
  const uint16_t rays = rgb565(0xF4A623);
  const uint16_t rim = rgb565(0xFFB52B);
  const uint16_t core = rgb565(0xFFE36A);
  for (int a = 0; a < 8; a++) {
    float ang = a * 3.14159f / 4.0f;
    int x1 = cx + (int)((r + 2) * cosf(ang)), y1 = cy + (int)((r + 2) * sinf(ang));
    int x2 = cx + (int)((r + 4) * cosf(ang)), y2 = cy + (int)((r + 4) * sinf(ang));
    gDisplay->drawLine(x1, y1, x2, y2, rays);
  }
  gDisplay->fillCircle(cx, cy, r, rim);
  gDisplay->fillCircle(cx - 1, cy - 1, r - 2, core);
  gDisplay->drawPixel(cx - 2, cy - 2, COL_WHITE);
}

static void moonBig(int cx, int cy) {
  gDisplay->fillCircle(cx, cy, 7, rgb565(0x46658B));
  gDisplay->fillCircle(cx - 1, cy - 1, 6, rgb565(0xD9E9FF));
  gDisplay->fillCircle(cx + 4, cy - 3, 6, 0);
  gDisplay->drawPixel(cx - 4, cy - 4, COL_WHITE);
}

static void cloudBig(int cx, int cy) {
  const uint16_t shadow = rgb565(0x46627D);
  const uint16_t base = rgb565(0x91A9BE);
  const uint16_t light = rgb565(0xD7E4EC);
  gDisplay->fillCircle(cx - 5, cy + 2, 4, shadow);
  gDisplay->fillCircle(cx + 1, cy - 1, 5, shadow);
  gDisplay->fillCircle(cx + 6, cy + 2, 4, shadow);
  gDisplay->fillRect(cx - 5, cy + 2, 12, 5, shadow);
  gDisplay->fillCircle(cx - 5, cy, 3, base);
  gDisplay->fillCircle(cx + 1, cy - 2, 4, base);
  gDisplay->fillCircle(cx + 6, cy + 1, 3, base);
  gDisplay->fillRect(cx - 5, cy + 1, 12, 4, base);
  gDisplay->fillCircle(cx - 1, cy - 3, 2, light);
  gDisplay->drawPixel(cx - 5, cy - 1, light);
}

static void iconBig(int cx, int cy, int code, bool isDay) {
  if (code == 0 || code == 1) {
    if (isDay) sunBig(cx, cy, 5); else moonBig(cx, cy);
  } else if (code == 2) {
    if (isDay) sunBig(cx + 4, cy - 4, 4);
    cloudBig(cx, cy + 3);
  } else if (code == 3) {
    cloudBig(cx, cy);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 4; i++) gDisplay->drawFastHLine(cx - 9 + (i % 2) * 3, cy - 6 + i * 4, 16, COL_GRAY);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    cloudBig(cx, cy - 3);
    for (int i = 0; i < 3; i++) {
      gDisplay->drawLine(cx - 5 + i * 5, cy + 5, cx - 7 + i * 5, cy + 9, rgb565(0x48B8FF));
      gDisplay->drawPixel(cx - 7 + i * 5, cy + 10, COL_WHITE);
    }
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    cloudBig(cx, cy - 3);
    for (int i = 0; i < 3; i++) gDisplay->fillRect(cx - 6 + i * 5, cy + 6 + (i % 2) * 3, 2, 2, COL_WHITE);
  } else if (code >= 95) {
    cloudBig(cx, cy - 3);
    gDisplay->drawLine(cx + 1, cy + 4, cx - 2, cy + 8, COL_YELLOW);
    gDisplay->drawLine(cx - 2, cy + 8, cx + 2, cy + 8, COL_YELLOW);
    gDisplay->drawLine(cx + 2, cy + 8, cx - 1, cy + 12, COL_YELLOW);
  } else {
    cloudBig(cx, cy);
  }
}

// ---------------- small icons (13 x 11) ----------------
static void sunSmall(int cx, int cy, int r) {
  const uint16_t ray = rgb565(0xF4A623);
  const uint16_t rim = rgb565(0xFFB52B);
  const uint16_t core = rgb565(0xFFE36A);
  const int d = r + 2;
  gDisplay->drawPixel(cx - d, cy, ray);
  gDisplay->drawPixel(cx + d, cy, ray);
  gDisplay->drawPixel(cx, cy - d, ray);
  gDisplay->drawPixel(cx, cy + d, ray);
  const int e = d - 1;
  gDisplay->drawPixel(cx - e, cy - e, ray);
  gDisplay->drawPixel(cx + e, cy - e, ray);
  gDisplay->drawPixel(cx - e, cy + e, ray);
  gDisplay->drawPixel(cx + e, cy + e, ray);
  gDisplay->fillCircle(cx, cy, r, rim);
  gDisplay->drawPixel(cx - 1, cy - 1, core);
}

static void cloudSmall(int cx, int cy) {
  const uint16_t shadow = rgb565(0x46627D);
  const uint16_t base = rgb565(0xA9BDCF);
  gDisplay->fillCircle(cx - 3, cy + 1, 2, shadow);
  gDisplay->fillCircle(cx, cy - 1, 3, shadow);
  gDisplay->fillCircle(cx + 3, cy + 1, 2, shadow);
  gDisplay->fillRect(cx - 3, cy + 1, 7, 3, shadow);
  gDisplay->fillCircle(cx, cy - 2, 2, base);
  gDisplay->fillRect(cx - 3, cy, 7, 3, base);
  gDisplay->drawPixel(cx - 2, cy - 1, COL_WHITE);
}

static void iconSmall(int cx, int cy, int code, bool isDay) {
  if (code == 0 || code == 1) {
    if (isDay) sunSmall(cx, cy, 2);
    else {
      gDisplay->fillCircle(cx, cy, 4, COL_WHITE);
      gDisplay->fillCircle(cx + 2, cy - 1, 3, 0);
    }
  } else if (code == 2) {
    if (isDay) sunSmall(cx + 3, cy - 3, 1);
    cloudSmall(cx - 1, cy + 2);
  } else if (code == 3) {
    cloudSmall(cx, cy);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 3; i++) gDisplay->drawFastHLine(cx - 5 + (i % 2) * 2, cy - 3 + i * 3, 9, COL_GRAY);
  } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    cloudSmall(cx, cy - 2);
    for (int i = 0; i < 3; i++) gDisplay->drawLine(cx - 3 + i * 3, cy + 3, cx - 4 + i * 3, cy + 5, rgb565(0x48B8FF));
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    cloudSmall(cx, cy - 2);
    for (int i = 0; i < 3; i++) gDisplay->drawPixel(cx - 3 + i * 3, cy + 3 + (i % 2) * 2, COL_WHITE);
  } else if (code >= 95) {
    cloudSmall(cx, cy - 2);
    gDisplay->drawLine(cx + 1, cy + 2, cx - 1, cy + 4, COL_YELLOW);
    gDisplay->drawLine(cx - 1, cy + 4, cx + 1, cy + 4, COL_YELLOW);
    gDisplay->drawLine(cx + 1, cy + 4, cx - 1, cy + 6, COL_YELLOW);
  } else {
    cloudSmall(cx, cy);
  }
}

void drawWxIcon(int cx, int cy, int code, bool isDay, bool big) {
  if (big) iconBig(cx, cy, code, isDay);
  else     iconSmall(cx, cy, code, isDay);
}

static void sunDetailed(int cx, int cy) {
  const uint16_t ray = rgb565(0xF4A623);
  const uint16_t rim = rgb565(0xFFB52B);
  const uint16_t core = rgb565(0xFFE36A);
  for (int i = 0; i < 8; i++) {
    const float a = i * 3.14159f / 4.0f;
    const int x1 = cx + (int)(9 * cosf(a)), y1 = cy + (int)(9 * sinf(a));
    const int x2 = cx + (int)(11 * cosf(a)), y2 = cy + (int)(11 * sinf(a));
    gDisplay->drawLine(x1, y1, x2, y2, ray);
  }
  gDisplay->fillCircle(cx, cy, 7, rim);
  gDisplay->fillCircle(cx - 1, cy - 1, 5, core);
  gDisplay->drawPixel(cx - 3, cy - 4, COL_WHITE);
}

static void moonDetailed(int cx, int cy) {
  const uint16_t shadow = rgb565(0x46658B);
  const uint16_t moon = rgb565(0xD9E9FF);
  gDisplay->fillCircle(cx, cy, 8, shadow);
  gDisplay->fillCircle(cx - 1, cy - 1, 7, moon);
  gDisplay->fillCircle(cx + 3, cy - 4, 7, 0);
  gDisplay->drawPixel(cx - 4, cy - 4, COL_WHITE);
}

static void cloudDetailed(int cx, int cy) {
  const uint16_t shadow = rgb565(0x46627D);
  const uint16_t base = rgb565(0x91A9BE);
  const uint16_t light = rgb565(0xD7E4EC);
  gDisplay->fillCircle(cx - 7, cy + 2, 5, shadow);
  gDisplay->fillCircle(cx, cy - 3, 7, shadow);
  gDisplay->fillCircle(cx + 8, cy + 2, 5, shadow);
  gDisplay->fillRect(cx - 7, cy + 2, 16, 7, shadow);
  gDisplay->fillCircle(cx - 7, cy, 4, base);
  gDisplay->fillCircle(cx, cy - 4, 6, base);
  gDisplay->fillCircle(cx + 7, cy + 1, 4, base);
  gDisplay->fillRect(cx - 7, cy + 1, 15, 5, base);
  gDisplay->fillCircle(cx - 2, cy - 5, 4, light);
  gDisplay->fillCircle(cx - 7, cy - 1, 2, light);
}

void drawWxIconDetailed(int cx, int cy, int code, bool isDay) {
  const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
  const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;

  if (code == 0 || code == 1) {
    if (isDay) sunDetailed(cx, cy); else moonDetailed(cx, cy);
    return;
  }
  if (code == 2) {
    if (isDay) sunDetailed(cx + 6, cy - 6); else moonDetailed(cx + 6, cy - 6);
    cloudDetailed(cx - 1, cy + 3);
    return;
  }
  if (code == 45 || code == 48) {
    const uint16_t fog = rgb565(0xAFC4D0);
    for (int i = 0; i < 4; i++)
      gDisplay->drawFastHLine(cx - 11 + (i & 1) * 3, cy - 7 + i * 5, 20 - (i & 1) * 3, fog);
    return;
  }

  cloudDetailed(cx, cy - 3);
  if (rain) {
    const uint16_t drop = rgb565(0x48B8FF);
    for (int i = 0; i < 3; i++) {
      const int x = cx - 7 + i * 7;
      gDisplay->drawLine(x, cy + 7, x - 2, cy + 11, drop);
      gDisplay->drawPixel(x - 2, cy + 12, COL_WHITE);
    }
  } else if (snow) {
    for (int i = 0; i < 3; i++) {
      const int x = cx - 7 + i * 7, y = cy + 9 + (i & 1) * 2;
      gDisplay->drawFastHLine(x - 2, y, 5, COL_WHITE);
      gDisplay->drawFastVLine(x, y - 2, 5, COL_WHITE);
      gDisplay->drawPixel(x - 1, y - 1, COL_WHITE);
      gDisplay->drawPixel(x + 1, y + 1, COL_WHITE);
    }
  } else if (code >= 95) {
    const uint16_t bolt = rgb565(0xFFE44D);
    gDisplay->drawLine(cx + 3, cy + 6, cx, cy + 11, bolt);
    gDisplay->drawLine(cx, cy + 11, cx + 3, cy + 11, bolt);
    gDisplay->drawLine(cx + 3, cy + 11, cx - 1, cy + 16, bolt);
  }
}
