#include "Countdown.h"
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../../core/SmoothFont.h"

static const uint32_t DEF_COL_LABEL  = 0xFFD200;
static const uint32_t DEF_COL_NUMBER = 0xFFFFFF;
static const uint32_t DEF_COL_ACCENT = 0x00C8FF;

static const int LABEL_MAX = 10;    // 10 characters * 6 px = 60 px, fits the 64 px panel

// Keep only printable ASCII (the panel font has no other characters).
static String cleanLabel(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && (int)out.length() < LABEL_MAX; i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

static int daysInMonth(int y, int m) {
  static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}

static bool parseDate(const String &s, int &y, int &m, int &d) {
  if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
  if (y < 1970 || y > 2100 || m < 1 || m > 12) return false;
  if (d < 1 || d > daysInMonth(y, m)) return false;
  return true;
}

// ---------- settings ----------
void CountdownModule::onLoad() {
  _label      = cleanLabel(store.getString("lab", "New Year"));
  _date       = store.getString("date", "2027-01-01");
  _hidePassed = store.getBool("hide", false);
  _repeat     = store.getBool("rep", false);
  _hourglass  = store.getBool("hg", true);
  _colLabel   = store.getUInt("cLab", DEF_COL_LABEL);
  _colNumber  = store.getUInt("cNum", DEF_COL_NUMBER);
  _colAccent  = store.getUInt("cAcc", DEF_COL_ACCENT);
  int y, m, d;
  if (!parseDate(_date, y, m, d)) _date = "2027-01-01";
}

String CountdownModule::onSettingsHtml() {
  String h;
  h += uiSection("What are you counting down to?");
  h += uiText("label", "Label (up to 10 characters)", _label, LABEL_MAX);
  h += uiDate("date", "Date", _date);
  h += uiCheckbox("rep", "Repeat every year (birthdays, holidays)", _repeat);
  h += uiCheckbox("hide", "Hide this page once the date has passed", _hidePassed);

  h += uiSection("Look");
  h += uiCheckbox("hg", "Animated hourglass while counting down", _hourglass);
  h += "<p class='m'>Countdown always shows whole days remaining. After the date has passed, it shows how many "
       "whole days ago it was.</p>";

  h += uiSection("Colors");
  h += uiColor("cLab", "Label", _colLabel);
  h += uiColor("cNum", "Numbers", _colNumber);
  h += uiColor("cAcc", "Small text (units, time and date)", _colAccent);
  return h;
}

void CountdownModule::onSave(WebServer &server) {
  String l = cleanLabel(server.arg("label"));
  if (l.length() > 0) _label = l;

  String dt = server.arg("date");
  dt.trim();
  int y, m, d;
  if (parseDate(dt, y, m, d)) _date = dt;

  _repeat     = server.hasArg("rep");
  _hourglass  = server.hasArg("hg");
  _hidePassed = server.hasArg("hide");

  uint32_t c;
  if (uiReadColor(server, "cLab", c)) _colLabel = c;
  if (uiReadColor(server, "cNum", c)) _colNumber = c;
  if (uiReadColor(server, "cAcc", c)) _colAccent = c;

  store.putString("lab", _label);
  store.putString("date", _date);
  store.putBool("rep", _repeat);
  store.putBool("hg", _hourglass);
  store.putBool("hide", _hidePassed);
  store.putUInt("cLab", _colLabel);
  store.putUInt("cNum", _colNumber);
  store.putUInt("cAcc", _colAccent);
  _availabilityKnown = false;
}

String CountdownModule::actionsHtml() {
  return "<form method='POST' action='/countdown/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void CountdownModule::registerRoutes(WebServer &server) {
  server.on("/countdown/colors-reset", HTTP_POST, [this]() {
    _colLabel = DEF_COL_LABEL; _colNumber = DEF_COL_NUMBER; _colAccent = DEF_COL_ACCENT;
    store.putUInt("cLab", _colLabel);
    store.putUInt("cNum", _colNumber);
    store.putUInt("cAcc", _colAccent);
    app.requestRedraw();
    webRedirect("/countdown?saved=1");
  });
}

// ---------- date math ----------
bool CountdownModule::dateParts(int &y, int &m, int &d) const {
  return parseDate(_date, y, m, d);
}

// The date being counted to. With "repeat every year", a date that already went by
// rolls forward to its next occurrence.
bool CountdownModule::resolveTarget(const struct tm &now, int &y, int &m, int &d) const {
  if (!dateParts(y, m, d)) return false;
  if (_repeat) {
    int ty = now.tm_year + 1900;
    long today = daysFromCivil(ty, now.tm_mon + 1, now.tm_mday);
    y = ty;
    if (daysFromCivil(y, m, d) < today) y = ty + 1;
  }
  return true;
}

// days = whole calendar days from today until the target (negative = in the past).
bool CountdownModule::daysLeft(long &days, long &today) const {
  struct tm t;
  if (!timeNow(t)) return false;
  int y, m, d;
  if (!resolveTarget(t, y, m, d)) return false;
  today = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  days = daysFromCivil(y, m, d) - today;
  return true;
}

String CountdownModule::summary() {
  long days, today;
  if (!daysLeft(days, today)) return _label + " (waiting for the time)";
  if (days == 0) return _label + ": today";
  if (days > 0)  return _label + ": " + String(days) + (days == 1 ? " day" : " days");
  return _label + ": " + String(-days) + ((-days) == 1 ? " day ago" : " days ago");
}

bool CountdownModule::pageAvailable(int sub) {
  (void)sub;
  long days, today;
  if (!daysLeft(days, today)) return _availabilityKnown && _lastAvailable;
  _lastAvailable = !_hidePassed || days >= 0;
  _availabilityKnown = true;
  return _lastAvailable;
}

// ---- days:hours left ----
// The target is midnight at the start of its date. days:hours count down to it and, once it has
// passed, up from it (past = true; on the date itself today = true).
bool CountdownModule::remaining(const struct tm &now, long &days, long &hours, bool &past, bool &today) const {
  int y, m, d;
  if (!resolveTarget(now, y, m, d)) return false;
  long nowSec = now.tm_hour * 3600L + now.tm_min * 60 + now.tm_sec;
  long dd = daysFromCivil(y, m, d) - daysFromCivil(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  long secs;
  if (dd > 0) secs = dd * 86400L - nowSec;
  else        secs = -dd * 86400L + nowSec;
  past = dd < 0;
  today = dd == 0;
  days = secs / 86400L;
  if (days > 99999) days = 99999;
  hours = (secs % 86400L) / 3600L;
  return true;
}

// The hourglass is 9 x 16 pixels. In landscape it sits at the right, beside the day count;
// in portrait it sits centered, in its own row under the divider. It is only there while counting down.
static const int HG_W = 9, HG_H = 16, HG_CTR = HG_W / 2;

bool CountdownModule::showHourglass(bool past, bool today) const {
  if (!_hourglass || past || today) return false;
  const int W = gDisplay->width(), H = gDisplay->height();
  if (H > W) return H >= 46;                       // portrait: its own row needs headroom above the number/footer
  return H >= 8 + HG_H && W >= 42;                 // landscape: beside the day count, needs width to spare
}

// Landscape updates daily unless its animated hourglass is visible.
long CountdownModule::redrawKey(const struct tm &t) const {
  if (gDisplay && gDisplay->height() > gDisplay->width()) return 2000000000L + (long)(millis() / 70);
  long day = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  long dd, hh;
  bool past, today;
  if (remaining(t, dd, hh, past, today) && showHourglass(past, today)) return 1000000000L + (long)(millis() / 50);
  return day;
}

bool CountdownModule::needsRedraw() {
  struct tm t;
  if (!timeNow(t)) return false;
  return redrawKey(t) != _lastKey;
}

// ---------- drawing ----------
void CountdownModule::drawPage(int sub) {
  (void)sub;
  struct tm t;
  if (!timeNow(t)) return;
  _lastKey = redrawKey(t);
  if (gDisplay->height() > gDisplay->width()) drawPortrait(t);
  else drawTwoRows(t);                                // landscape always uses the days-only layout
}

// ---- the hourglass ----
// Drawn from a small picture (buf) so it can be spun in place for the reset. The glass is 14 rows
// between two caps; HG_HALF is the width of the inside of each row.
static const uint8_t HG_HALF[14] = {7, 7, 7, 5, 5, 1, 1,  1, 1, 5, 5, 7, 7, 7};
static const int      HG_CELLS = 33;                       // places for sand in one half
static const uint32_t HG_DRAIN_MS = 9000, HG_REST_MS = 500, HG_FLIP_MS = 600;
static const uint32_t HG_CYCLE_MS = HG_DRAIN_MS + HG_REST_MS + HG_FLIP_MS;

// The k-th grain of the top bulb (k = 0 is next to the neck) or of the pile at the bottom (k = 0 is at
// the very bottom). Each row fills from its middle outward.
static bool hgCell(bool top, int k, int &row, int &col) {
  for (int r = top ? 7 : 14, last = top ? 1 : 8; r >= last; r--) {
    int w = HG_HALF[r - 1];
    if (k < w) {
      int off = (k + 1) / 2;
      col = HG_CTR + ((k & 1) ? -off : off);
      row = r;
      return true;
    }
    k -= w;
  }
  return false;
}

// Lightens (delta > 0, toward white) or darkens (delta < 0, toward black) an 0xRRGGBB color by
// delta percent per channel, same light-from-upper-left trick the weather icons and the grandfather
// clock's wood case use: a shadow tone and a light tone of the same base color, not a flat fill.
static int clampByte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
static uint16_t shade(uint32_t rrggbb, int delta) {
  int r = (rrggbb >> 16) & 0xFF, g = (rrggbb >> 8) & 0xFF, b = rrggbb & 0xFF;
  if (delta >= 0) {
    r = clampByte(r + (255 - r) * delta / 100);
    g = clampByte(g + (255 - g) * delta / 100);
    b = clampByte(b + (255 - b) * delta / 100);
  } else {
    r = clampByte(r + r * delta / 100);
    g = clampByte(g + g * delta / 100);
    b = clampByte(b + b * delta / 100);
  }
  return rgb565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

void CountdownModule::drawHourglass(int x, int y) {
  uint32_t t = millis() % HG_CYCLE_MS;
  int drained = 0;                                          // grains that have fallen to the bottom
  bool falling = false;
  float theta = 0.0f;                                       // spin angle: 0 -> PI (180 degrees) during the reset
  if (t < HG_DRAIN_MS) {
    drained = (int)((uint64_t)t * HG_CELLS / HG_DRAIN_MS);
    falling = true;
  } else if (t < HG_DRAIN_MS + HG_REST_MS) {
    drained = HG_CELLS;
  } else {
    // Full and still while it spins a continuous 180 degrees in place, like a record on a
    // turntable, rather than pinching flat and back out; the sand resets the instant the next
    // cycle begins, at which point the (symmetric) glass looks upright again either way.
    drained = HG_CELLS;
    const float PI_F = 3.14159265f;
    uint32_t f = t - HG_DRAIN_MS - HG_REST_MS;
    theta = PI_F * (float)f / (float)HG_FLIP_MS;
  }

  const uint16_t glassLight = shade(_colAccent, 35), glassDark = shade(_colAccent, -35);
  const uint16_t sandLight  = shade(_colLabel, 40),  sandDark  = shade(_colLabel, -30);
  const uint16_t glint = COL_WHITE;
  uint16_t buf[HG_H][HG_W];
  memset(buf, 0, sizeof(buf));

  // caps and side walls: lit on the left (where the light comes from), shadowed on the right,
  // just like the sun/moon/cloud icons and the clock case's wood/woodLight bevel.
  for (int c = 0; c < HG_W; c++) {
    buf[0][c] = (c <= HG_CTR) ? glassLight : glassDark;
    buf[HG_H - 1][c] = glassDark;
  }
  for (int r = 1; r <= 14; r++) {
    int w = HG_HALF[r - 1], lo = HG_CTR - (w - 1) / 2, hi = HG_CTR + (w - 1) / 2;
    buf[r][lo - 1] = glassLight;
    buf[r][hi + 1] = glassDark;
  }
  buf[0][1] = glint;                                        // a single bright glass glint, same touch the weather icons use

  // sand: the top bulb's waiting grains catch the light; the settled pile is lit only on its
  // freshly-fallen top layer, with the rest of the pile shaded underneath.
  int row, col, surface = HG_H - 1;                         // surface: highest row of the bottom pile
  for (int k = 0; k < drained; k++) if (hgCell(false, k, row, col)) if (row < surface) surface = row;
  for (int k = 0; k < HG_CELLS - drained; k++) if (hgCell(true, k, row, col)) buf[row][col] = sandLight;
  for (int k = 0; k < drained; k++) if (hgCell(false, k, row, col)) buf[row][col] = (row <= surface + 1) ? sandLight : sandDark;
  if (falling) {                                            // the thin stream, flickering
    long frame = (long)(millis() / 100);
    for (int r = 8; r < surface; r++) if (((r + frame) & 1) == 0) buf[r][HG_CTR] = sandLight;
  }

  const float cx = (HG_W - 1) / 2.0f, cy = (HG_H - 1) / 2.0f;
  const float cs = cosf(theta), sn = sinf(theta);
  for (int r = 0; r < HG_H; r++)
    for (int c = 0; c < HG_W; c++)
      if (buf[r][c]) {
        float dx = c - cx, dy = r - cy;
        int sx = x + (int)lroundf(cx + dx * cs - dy * sn);
        int sy = y + (int)lroundf(cy + dx * sn + dy * cs);
        gDisplay->drawPixel(sx, sy, buf[r][c]);
      }
}

// Big-number font fitting (SmoothFit / fitSmoothFont / SMOOTH_FONTS) now lives in
// src/core/SmoothFont.h, shared with Clock, Weather and Gas.

void CountdownModule::drawPortrait(const struct tm &now) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const uint16_t cLabel = rgb565(_colLabel);
  const uint16_t cNum = rgb565(_colNumber);
  const uint16_t cAcc = rgb565(_colAccent);

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  const int labelW = _label.length() * 6 - 1;
  const int labelX = labelW <= W - 2 ? (W - labelW) / 2
                                     : W - (int)((millis() / 70) % (W + labelW + 1));
  gDisplay->setClipX(0, W);
  gDisplay->setTextColor(cLabel);
  gDisplay->setCursor(labelX, 0);
  gDisplay->print(_label);
  gDisplay->clearClip();
  gDisplay->drawFastHLine(1, 8, W - 2, cAcc);

  String mainValue;
  String footer;
  bool hg = false;
  {
    long days, hours;
    bool past, today;
    if (!remaining(now, days, hours, past, today)) return;
    if (today) {
      mainValue = "TODAY";
      footer = "TARGET DATE";
    } else {
      mainValue = String(past ? "+" : "") + String(days);
      footer = String(past ? "AGO" : "DAYS");
    }
    hg = showHourglass(past, today);
  }
  const int hgRowH = hg ? HG_H + 2 : 0;               // its own row, right under the divider

  const int maxW = W - 2, maxH = H - 18 - hgRowH;
  SmoothFit fit = fitSmoothFont(mainValue.c_str(), maxW, maxH);
  if (hg) drawHourglass((W - HG_W) / 2, 10);
  gDisplay->setTextColor(cNum);
  int16_t valueX, valueTop;
  uint16_t valueW, valueH;
  int valueY;
  if (fit.font) {
    valueW = fit.w; valueH = fit.h; valueX = fit.xBearing;
    valueTop = (int16_t)(10 + hgRowH + (maxH - (int)valueH) / 2);
    valueY = valueTop - fit.top;
  } else {
    gDisplay->setTextSize(1);
    valueY = 10 + hgRowH + (maxH - 7) / 2;
    gDisplay->getTextBounds(mainValue.c_str(), 0, valueY, &valueX, &valueTop, &valueW, &valueH);
  }
  gDisplay->setCursor((W - (int)valueW) / 2 - valueX, valueY);
  gDisplay->print(mainValue);
  gDisplay->antialiasText((W - (int)valueW) / 2, valueTop, (int)valueW, (int)valueH, cNum);
  gDisplay->setFont(nullptr);
  gDisplay->setTextSize(1);

  const int footerW = footer.length() * 6 - 1;
  const int footerX = footerW <= W - 2 ? (W - footerW) / 2
                                        : W - (int)((millis() / 70) % (W + footerW + 1));
  gDisplay->setTextSize(1);
  gDisplay->setTextColor(cAcc);
  gDisplay->setClipX(0, W);
  gDisplay->setCursor(footerX, H - 7);
  gDisplay->print(footer);
  gDisplay->clearClip();
  gDisplay->flipDMABuffer();
}

// Landscape layout: the message on the first row, then the day count and optional hourglass.
void CountdownModule::drawTwoRows(const struct tm &now) {
  long dd, hh;
  bool past, today;
  if (!remaining(now, dd, hh, past, today)) return;
  int ty, tm, td;
  if (!resolveTarget(now, ty, tm, td)) return;
  long cal = daysFromCivil(ty, tm, td) - daysFromCivil(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  if (cal < 0) cal = -cal;
  if (cal > 99999) cal = 99999;

  const int W = gDisplay->width(), H = gDisplay->height();
  const bool hg = showHourglass(past, today);
  const int hgX = W - HG_W - 1, hgY = 8 + (H - 8 - HG_H) / 2;
  const int right = hg ? hgX - 1 : W - 1;                   // the numbers must end before this
  const bool big = H >= 22;
  uint16_t cNum = rgb565(_colNumber), cAcc = rgb565(_colAccent);

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);
  gDisplay->setTextColor(rgb565(_colLabel));
  gDisplay->setCursor(2, 0);
  gDisplay->print(_label);

  if (today) {
    const int maxH2 = H - 8;
    SmoothFit fit = fitSmoothFont("TODAY", right - 2, maxH2);
    gDisplay->setTextColor(cNum);
    int16_t tx, ttop;
    uint16_t tw, th;
    int ty;
    if (fit.font) {
      tw = fit.w; th = fit.h; tx = fit.xBearing;
      ttop = (int16_t)(8 + (maxH2 - (int)th) / 2);
      ty = ttop - fit.top;
    } else {
      gDisplay->setTextSize(1);
      ty = 8 + (maxH2 - 7) / 2;
      gDisplay->getTextBounds("TODAY", 0, ty, &tx, &ttop, &tw, &th);
    }
    gDisplay->setCursor(2 - tx, ty);
    gDisplay->print("TODAY");
    gDisplay->antialiasText(2, ttop, (int)tw, (int)th, cNum);
    gDisplay->setFont(nullptr);
    gDisplay->setTextSize(1);
  } else {
    // the number of days with its unit small beside it: "12 days", "3 d", "40 ago"
    char dn[12];
    snprintf(dn, sizeof(dn), "%ld", cal);
    const int nd = (int)strlen(dn);
    const char *units[2] = {past ? "d ago" : (cal == 1 ? "day" : "days"), past ? "ago" : "d"};
    const int maxH3 = H - 8;

    // Candidates, biggest first: each of the Helvetica-like fonts, then the classic bitmap font
    // at size 2 (landscape's old "big" tier) and finally size 1, which always fits something.
    struct Cand { const GFXfont *font; int sz; };
    Cand cands[SMOOTH_FONT_COUNT + 2];
    int nc = 0;
    for (int i = 0; i < SMOOTH_FONT_COUNT; i++) cands[nc++] = {SMOOTH_FONTS[i], 0};
    if (big) cands[nc++] = {nullptr, 2};
    cands[nc++] = {nullptr, 1};

    const GFXfont *numFont = nullptr;
    int numSz = 1, ui = 1;
    int16_t numX = 0, numTop = 0;
    uint16_t numW = 0, numH = 7;
    bool found = false;
    gDisplay->setTextSize(1);        // custom fonts are still scaled by the classic font's size multiplier
    for (int ci = 0; ci < nc && !found; ci++) {
      int16_t x, top;
      uint16_t w, h;
      if (cands[ci].font) {
        gDisplay->setFont(cands[ci].font);
        gDisplay->getTextBounds(dn, 0, 0, &x, &top, &w, &h);
      } else {
        gDisplay->setFont(nullptr);
        const int sz = cands[ci].sz;
        x = 0; top = 0; w = nd * 6 * sz - sz; h = 7 * sz;  // the classic font's own fixed metrics
      }
      if ((int)h > maxH3) continue;
      for (int u = 0; u < 2 && !found; u++)                 // the longest unit that still fits, then the shorter one
        if (2 + (int)w + 3 + (int)strlen(units[u]) * 6 <= right) {
          numFont = cands[ci].font; numSz = cands[ci].sz;
          numX = x; numTop = top; numW = w; numH = h; ui = u; found = true;
        }
    }

    int y, top;
    if (numFont) {
      gDisplay->setFont(numFont);
      top = 8 + (maxH3 - (int)numH) / 2;
      y = top - numTop;
    } else {
      gDisplay->setFont(nullptr);
      gDisplay->setTextSize(numSz);
      y = 8 + (maxH3 - (int)numH) / 2;
      top = y;
    }
    gDisplay->setTextColor(cNum);
    gDisplay->setCursor(2 - numX, y);
    gDisplay->print(dn);
    gDisplay->antialiasText(2, top, (int)numW, (int)numH, cNum);
    gDisplay->setFont(nullptr);
    gDisplay->setTextSize(1);
    gDisplay->setTextColor(cAcc);
    gDisplay->setCursor(2 + (int)numW + 3, top + (int)numH - 7);
    gDisplay->print(units[ui]);
  }
  if (hg) drawHourglass(hgX, hgY);
  gDisplay->flipDMABuffer();
}
