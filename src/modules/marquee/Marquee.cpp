#include "Marquee.h"
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/TimeService.h"
#include "../../core/App.h"

static const uint32_t DEF_COL_TEXT = 0xFFFFFF;
static const uint32_t DEF_COL_ANTS = 0xFFB000;
static const uint32_t DEF_COL_GAP  = 0x000000;

static const unsigned long TEXT_STEP  = 50;     // ms per pixel of scrolling

// Keep only printable ASCII (the panel font has no other characters).
static String cleanLine(const String &in, int maxLen) {
  String out;
  for (size_t i = 0; i < in.length() && (int)out.length() < maxLen; i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

static bool validDate(const String &s) {
  int y, m, d;
  if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
  return y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

bool MarqueeModule::todayString(String &out) {
  struct tm t;
  if (!timeNow(t)) return false;
  char b[24];
  snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  out = b;
  return true;
}

bool MarqueeModule::isToday(const String &date) const {
  String today;
  return todayString(today) && today == date;
}

// With "only today's message", the marquee is shown only on the day it was saved.
bool MarqueeModule::pageAvailable(int sub) {
  (void)sub;
  if (!_onlyDay) return true;
  if (!validDate(_day)) {                                // switched on but no day recorded yet: today
    String t;
    if (!todayString(t)) return false;
    _day = t;
    store.putString("day", _day);
  }
  return isToday(_day);
}

// ---------- settings ----------
void MarqueeModule::onLoad() {
  _onlyDay = store.getBool("only", false);
  _day = store.getString("day", "");
  if (!validDate(_day)) _day = "";
  _line[0] = cleanLine(store.getString("l1", "Welcome"), MQ_LINE_MAX);
  _line[1] = cleanLine(store.getString("l2", "Home"), MQ_LINE_MAX);
  _line[2] = cleanLine(store.getString("l3", ""), MQ_LINE_MAX);
  _style = store.getU8("sty", 0);  if (_style > 2) _style = 0;
  _speed = store.getU8("spd", 1);  if (_speed > 2) _speed = 1;
  _dir   = store.getU8("dir", 0) ? 1 : 0;
  _colText = store.getUInt("cTxt", DEF_COL_TEXT);
  _colAnts = store.getUInt("cAnt", DEF_COL_ANTS);
  _colGap  = store.getUInt("cGap", DEF_COL_GAP);
}

String MarqueeModule::onSettingsHtml() {
  String h;
  h += uiSection("Text (one to three lines)");
  h += uiText("l1", "Line 1", _line[0], MQ_LINE_MAX);
  h += uiText("l2", "Line 2 (leave empty to skip)", _line[1], MQ_LINE_MAX);
  h += uiText("l3", "Line 3 (leave empty to skip)", _line[2], MQ_LINE_MAX);
  h += "<p class='m'>Empty lines are skipped, so one line is shown large and centered when it fits. "
       "A line that is too wide for the screen pauses, then scrolls sideways.</p>";

  h += uiSection("When to show it");
  h += uiCheckbox("only", "Only show today's message (it is in the rotation only on the day you save it)", _onlyDay);
  if (_onlyDay && validDate(_day))
    h += "<p class='m'>Showing on " + _day + " only. Saving this page again on another day sets it for that day.</p>";
  else
    h += "<p class='m'>When this is on, the message drops out of the rotation after the day you save it. "
         "Save the page again on a new day to show it that day.</p>";

  h += uiSection("Ants");
  static const char *const styles[] = {"Marching dashes", "Dots", "Long dashes"};
  h += uiSelect("sty", "Style", styles, 3, _style);
  static const char *const speeds[] = {"Slow", "Normal", "Fast"};
  h += uiSelect("spd", "Speed", speeds, 3, _speed);
  static const char *const dirs[] = {"Clockwise", "Counter-clockwise"};
  h += uiSelect("dir", "Direction", dirs, 2, _dir);

  h += uiSection("Colors");
  h += uiColor("cTxt", "Text", _colText);
  h += uiColor("cAnt", "Ants", _colAnts);
  h += uiColor("cGap", "Between the ants (black = nothing)", _colGap);
  return h;
}

void MarqueeModule::onSave(WebServer &server) {
  _line[0] = cleanLine(server.arg("l1"), MQ_LINE_MAX);
  _line[1] = cleanLine(server.arg("l2"), MQ_LINE_MAX);
  _line[2] = cleanLine(server.arg("l3"), MQ_LINE_MAX);
  _onlyDay = server.hasArg("only");
  if (_onlyDay) { String t; if (todayString(t)) _day = t; }      // saving marks the message as today's
  _style = (uint8_t)uiReadLong(server, "sty", _style, 0, 2);
  _speed = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _dir   = (uint8_t)uiReadLong(server, "dir", _dir, 0, 1);

  uint32_t c;
  if (uiReadColor(server, "cTxt", c)) _colText = c;
  if (uiReadColor(server, "cAnt", c)) _colAnts = c;
  if (uiReadColor(server, "cGap", c)) _colGap = c;

  store.putBool("only", _onlyDay);
  store.putString("day", _day);
  store.putString("l1", _line[0]);
  store.putString("l2", _line[1]);
  store.putString("l3", _line[2]);
  store.putU8("sty", _style);
  store.putU8("spd", _speed);
  store.putU8("dir", _dir);
  store.putUInt("cTxt", _colText);
  store.putUInt("cAnt", _colAnts);
  store.putUInt("cGap", _colGap);
}

String MarqueeModule::actionsHtml() {
  return "<form method='POST' action='/marquee/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void MarqueeModule::registerRoutes(WebServer &server) {
  server.on("/marquee/colors-reset", HTTP_POST, [this]() {
    _colText = DEF_COL_TEXT; _colAnts = DEF_COL_ANTS; _colGap = DEF_COL_GAP;
    store.putUInt("cTxt", _colText);
    store.putUInt("cAnt", _colAnts);
    store.putUInt("cGap", _colGap);
    app.requestRedraw();
    webRedirect("/marquee?saved=1");
  });
}

int MarqueeModule::lineCount() const {
  int n = 0;
  for (int i = 0; i < 3; i++) if (_line[i].length() > 0) n++;
  return n;
}

String MarqueeModule::summary() {
  String s;
  for (int i = 0; i < 3; i++) {
    if (_line[i].length() == 0) continue;
    if (s.length()) s += " / ";
    s += _line[i];
  }
  if (s.length() == 0) s = "(no text)";
  if (_onlyDay) s += String(" - only on ") + (validDate(_day) ? _day : String("the day it is saved"));
  return s;
}

// ---------- drawing ----------
int MarqueeModule::antMs() const {
  static const int ms[3] = {160, 90, 50};       // time for the ants to move one pixel
  return ms[_speed];
}

// Redraw for every ant step and, while a long line scrolls, for every pixel it moves.
bool MarqueeModule::needsRedraw() {
  const unsigned long now = millis();
  if (now / (unsigned long)antMs() != _lastKey) return true;
  return _scrolling && (now - _scrollT0) / TEXT_STEP != _lastText;
}

// The ring of ants around the edge of the usable screen. Positions are numbered
// clockwise from the top left corner; each pixel's color depends on its position
// and on the current step, so the pattern slides along the ring.
void MarqueeModule::drawAnts(unsigned long step) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const int total = 2 * (W + H) - 4;
  static const int ANT_PERIOD[3] = {4, 3, 6};        // pattern length
  static const int ANT_ON[3]     = {2, 1, 3};        // ants (lit) part of the pattern
  const int period = ANT_PERIOD[_style], on = ANT_ON[_style];
  const uint16_t cAnt = rgb565(_colAnts), cGap = rgb565(_colGap);
  const bool showGap = _colGap != 0;

  for (int i = 0; i < total; i++) {
    int x, y;
    if (i < W)                   { x = i;                     y = 0; }
    else if (i < W + H - 1)      { x = W - 1;                 y = i - (W - 1); }
    else if (i < 2 * W + H - 2)  { x = (W - 1) - (i - (W + H - 2));  y = H - 1; }
    else                         { x = 0;                     y = (H - 1) - (i - (2 * W + H - 3)); }
    long phase = (_dir == 0) ? (long)i - (long)step : (long)i + (long)step;
    int k = (int)(((phase % period) + period) % period);
    if (k < on)        gDisplay->drawPixel(x, y, cAnt);
    else if (showGap)  gDisplay->drawPixel(x, y, cGap);
  }
}

void MarqueeModule::drawPage(int sub) {
  (void)sub;
  unsigned long now = millis();
  if (now - _lastDraw > 500) _scrollT0 = now;      // a fresh showing: scroll from the start
  _lastDraw = now;
  _scrolling = false;

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);

  drawAnts(now / antMs());

  // text area: inside the ring, with one pixel of air
  const int W = gDisplay->width(), H = gDisplay->height();
  const int x0 = 2, x1 = W - 2;                   // usable columns x0 <= x < x1
  const int areaW = x1 - x0, areaH = H - 4;
  int n = lineCount();
  const uint16_t cText = rgb565(_colText);

  const String *lines[3];
  int k = 0;
  for (int i = 0; i < 3; i++) if (_line[i].length() > 0) lines[k++] = &_line[i];
  static const String placeholder = "(no text)";
  if (n == 0) { lines[0] = &placeholder; n = 1; }

  int size = 1, lineH = 7;
  if (n == 1 && (int)lines[0]->length() * 12 - 2 <= areaW) { size = 2; lineH = 14; }   // large if it fits
  gDisplay->setTextSize(size);
  const int adv = 6 * size;
  const int gap = (areaH - lineH * n) / (n + 1);
  int y = 2 + gap;

  for (int i = 0; i < n; i++, y += lineH + gap) {
    const char *s = lines[i]->c_str();
    int tw = (int)lines[i]->length() * adv - size;
    gDisplay->setTextColor(cText);
    if (tw <= areaW) {
      gDisplay->setCursor(x0 + (areaW - tw) / 2, y);
      gDisplay->print(s);
    } else {
      // too wide: scroll continuously left through the area and loop around, no pausing (size 1 only)
      _scrolling = true;
      const int period = tw + 12;
      unsigned long t = (now - _scrollT0) % ((unsigned long)period * TEXT_STEP);
      int off = (int)(t / TEXT_STEP);
      gDisplay->setClipX(x0, x1);
      gDisplay->setCursor(x0 - off, y);
      gDisplay->print(s);
      gDisplay->setCursor(x0 - off + period, y);
      gDisplay->print(s);
      gDisplay->clearClip();
    }
  }
  gDisplay->setTextSize(1);
  _lastKey = now / (unsigned long)antMs();
  _lastText = (now - _scrollT0) / TEXT_STEP;
  gDisplay->flipDMABuffer();
}
