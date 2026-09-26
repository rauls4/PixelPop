#include "Seasonal.h"
#include <string.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../weather/Weather.h"

static const uint32_t DEF_COL_TITLE = 0x00C8FF;
static const uint32_t DEF_COL_FRUIT = 0xFFD200;
static const uint32_t DEF_COL_VEG   = 0x60E070;

static const char *const MONTHS[12] = {"January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December"};
static const char *const SPEED_NAMES[3] = {"Slow", "Normal", "Fast"};
static const unsigned long MS_PER_PX[3] = {45, 30, 20};

// ---------- settings ----------
void SeasonalModule::onLoad() {
  _region    = store.getU8("reg", 0);
  if (_region > REGION_COUNT) _region = 0;
  _month     = store.getU8("mon", 0);
  if (_month > 12) _month = 0;
  _fruits    = store.getBool("fr", true);
  _veg       = store.getBool("vg", true);
  _speed     = store.getU8("spd", 1);
  if (_speed > 2) _speed = 1;
  _maxItems  = store.getU8("cnt", 10);
  if (_maxItems > 40) _maxItems = 40;
  _colTitle  = store.getUInt("cT", DEF_COL_TITLE);
  _colFruit  = store.getUInt("cF", DEF_COL_FRUIT);
  _colVeg    = store.getUInt("cV", DEF_COL_VEG);
  _key = -1;
}

String SeasonalModule::onSettingsHtml() {
  refreshList();
  String h;
  h += uiSection("Where and when");
  static const char *regionOpts[REGION_COUNT + 1];
  regionOpts[0] = "Automatic (the location on the Weather page)";
  for (int i = 0; i < REGION_COUNT; i++) regionOpts[i + 1] = REGION_NAMES[i];
  h += uiSelect("reg", "Region", regionOpts, REGION_COUNT + 1, _region);
  static const char *monthOpts[13];
  monthOpts[0] = "This month";
  for (int i = 0; i < 12; i++) monthOpts[i + 1] = MONTHS[i];
  h += uiSelect("mon", "Month", monthOpts, 13, _month);
  h += String("<p class='m'>Showing: <b>") + REGION_NAMES[_regionNow] + "</b>, " + MONTHS[_monthNow - 1] +
       ". Winter citrus counts as in season everywhere in the United States, even where it does not grow.</p>";

  h += uiSection("What to show");
  h += uiCheckbox("fr", "Fruits", _fruits);
  h += uiCheckbox("vg", "Vegetables", _veg);
  h += uiNumber("cnt", "Most items to show each time (0 = all of them)", _maxItems, 0, 40);
  h += uiSelect("spd", "Scroll speed", SPEED_NAMES, 3, _speed);
  h += "<p class='m'>Each time the page comes round it picks items in season at random (or, with 0, carries on down the list), "
       "so it shows something different every time. It only takes as many as scroll all the way across and off the screen "
       "within the rotation time set for this function at the top of this page, so nothing is cut off; "
       "a longer rotation time or a faster scroll shows more.</p>";

  h += uiSection("Colors");
  h += uiColor("cT", "Title", _colTitle);
  h += uiColor("cF", "Fruit names", _colFruit);
  h += uiColor("cV", "Vegetable names", _colVeg);

  h += uiSection("In season now");
  String list;
  for (int i = 0; i < _n; i++) {
    if (i) list += ", ";
    list += PRODUCE[_idx[i]].name;
  }
  h += "<p class='m'>" + (_n ? list : String("Nothing to show with these choices.")) + "</p>";
  return h;
}

void SeasonalModule::onSave(WebServer &server) {
  _region = (uint8_t)uiReadLong(server, "reg", _region, 0, REGION_COUNT);
  _month  = (uint8_t)uiReadLong(server, "mon", _month, 0, 12);
  _fruits = server.hasArg("fr");
  _veg    = server.hasArg("vg");
  _speed  = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _maxItems = (uint8_t)uiReadLong(server, "cnt", _maxItems, 0, 40);
  uint32_t c;
  if (uiReadColor(server, "cT", c)) _colTitle = c;
  if (uiReadColor(server, "cF", c)) _colFruit = c;
  if (uiReadColor(server, "cV", c)) _colVeg = c;

  store.putU8("reg", _region);
  store.putU8("mon", _month);
  store.putBool("fr", _fruits);
  store.putBool("vg", _veg);
  store.putU8("spd", _speed);
  store.putU8("cnt", _maxItems);
  store.putUInt("cT", _colTitle);
  store.putUInt("cF", _colFruit);
  store.putUInt("cV", _colVeg);
  _key = -1;
}

String SeasonalModule::actionsHtml() {
  return "<form method='POST' action='/seasonal/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void SeasonalModule::registerRoutes(WebServer &server) {
  server.on("/seasonal/colors-reset", HTTP_POST, [this]() {
    _colTitle = DEF_COL_TITLE; _colFruit = DEF_COL_FRUIT; _colVeg = DEF_COL_VEG;
    store.putUInt("cT", _colTitle);
    store.putUInt("cF", _colFruit);
    store.putUInt("cV", _colVeg);
    app.requestRedraw();
    webRedirect("/seasonal?saved=1");
  });
}

// ---------- the list ----------
int SeasonalModule::activeRegion() const {
  if (_region > 0) return _region - 1;
  Module *m = app.find("weather");
  if (m) {
    WeatherModule *w = static_cast<WeatherModule *>(m);
    return regionForLocation(w->latitude(), w->longitude());
  }
  return RG_MIDWEST;
}

int SeasonalModule::activeMonth() const {
  if (_month > 0) return _month;
  struct tm t;
  if (timeNow(t)) return t.tm_mon + 1;
  return 0;                                                 // the clock has not synced yet
}

// Rebuilds the list when the region, the month or the choices have changed.
void SeasonalModule::refreshList() {
  const int region = activeRegion();
  int month = activeMonth();
  const int key = (region * 13 + month) * 4 + (_fruits ? 1 : 0) + (_veg ? 2 : 0);
  if (key == _key) return;
  _key = key;
  _regionNow = region;
  _monthNow = month > 0 ? month : 1;
  _n = 0;
  if (month > 0) {
    for (int i = 0; i < PRODUCE_COUNT && _n < MAX_ITEMS; i++) {
      const Produce &p = PRODUCE[i];
      if (p.veg ? !_veg : !_fruits) continue;
      if (produceInSeason(p, region, month)) _idx[_n++] = (uint8_t)i;
    }
  }
  _lap = 0;                                                 // worked out at the next draw (it depends on the size)
  if (_shownN == 0) _needPick = true;                       // (a showing already under way keeps its items until it is over)
}

uint32_t SeasonalModule::rnd() {                            // xorshift: plenty random for choosing produce
  if (_rng == 0x9E3779B9u) _rng ^= (uint32_t)millis() * 2654435761u + 12345u;
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

// The items for one showing. Only as many as scroll all the way across and off the screen within the
// rotation time, so no item is cut off when the page changes. With a limit: that many picked at random, in
// a random order. With "all of them": the list in its usual order, carrying on where the last showing stopped.
void SeasonalModule::pickItems() {
  _needPick = false;
  const int W = gDisplay->width(), H = gDisplay->height();
  const int s = (H - 8 >= 18) ? 2 : 1;
  long budget = (long)((seconds() * 1000UL - 300UL) / msPerPx()) - W;      // pixels of list that fit in the time
  if (budget < 1) budget = 1;

  int order[MAX_ITEMS];
  int want = _n;
  const bool random = (_maxItems > 0 && _maxItems < _n);
  if (random) {
    for (int i = 0; i < _n; i++) order[i] = _idx[i];
    want = _maxItems;
    for (int i = 0; i < want; i++) {                        // a partial shuffle: the first "want" places
      int j = i + (int)(rnd() % (uint32_t)(_n - i));
      int t = order[i]; order[i] = order[j]; order[j] = t;
    }
  } else {
    if (_start >= _n) _start = 0;
    for (int i = 0; i < _n; i++) order[i] = _idx[(_start + i) % _n];
  }
  int count = 0;
  long used = 0;
  for (int i = 0; i < want; i++) {
    const long w = itemWidth(PRODUCE[order[i]], s);
    if (used + w > budget) {
      if (random) continue;                                   // too wide for the time left: try a narrower pick
      if (count > 0) break;
    }
    _show[count++] = (uint8_t)order[i];
    used += w;
  }
  if (count == 0 && want > 0) _show[count++] = (uint8_t)order[0];     // even one does not fit: show it anyway
  if (!random && _n > 0) _start = (_start + count) % _n;
  _shownN = count;
  Serial.print("In Season: showing ");
  Serial.print(count);
  Serial.print(" items, ");
  Serial.print(used);
  Serial.print(" px of ");
  Serial.print(budget);
  Serial.println(" px that fit");
}

unsigned long SeasonalModule::msPerPx() const { return MS_PER_PX[_speed]; }

String SeasonalModule::summary() {
  refreshList();
  if (activeMonth() == 0) return "Waiting for the time";
  return String(REGION_NAMES[_regionNow]) + ", " + MONTHS[_monthNow - 1] + ": " + String(_n) + " in season";
}

// ---------- drawing ----------
bool SeasonalModule::pageAvailable(int sub) {
  (void)sub;
  refreshList();
  return _n > 0;
}

// A new frame exactly when the items have moved one more pixel: any other beat makes them move a pixel,
// then two, then one, which looks jerky.
bool SeasonalModule::needsRedraw() {
  return (millis() - _t0) / msPerPx() != _lastStep;
}

// One item: the picture, then the name. s is 2 when the screen is tall enough for the big pictures.
int SeasonalModule::itemWidth(const Produce &p, int s) const {
  return 9 * s + (2 + s) + (int)strlen(p.name) * 6 * s + 9 * s;
}

// A picture, one horizontal run of the same color at a time (fewer, bigger drawing calls than pixel by pixel:
// the frames are quicker to draw, and quick frames are what keep the scrolling and the title steady).
static void drawIcon(const Produce &p, int x, int y, int s, int W) {
  uint16_t pal[5];
  for (int i = 0; i < 5; i++) pal[i] = rgb565(p.color[i]);
  const char *const *rows = SPRITES[p.sprite];
  for (int r = 0; r < SPRITE_SIZE; r++) {
    const char *row = rows[r];
    int c = 0;
    while (c < SPRITE_SIZE) {
      const char ch = row[c];
      if (ch < '1' || ch > '5') { c++; continue; }
      int end = c;
      while (end < SPRITE_SIZE && row[end] == ch) end++;
      const int px = x + c * s, pw = (end - c) * s;
      if (px < W && px + pw > 0) gDisplay->fillRect(px, y + r * s, pw, s, pal[ch - '1']);
      c = end;
    }
  }
}

void SeasonalModule::drawPage(int sub) {
  (void)sub;
  refreshList();
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  if (_fresh || _done || now - _lastDraw > 60000UL) {         // a new showing (the rotation says so), or a new round
    _t0 = now; _fresh = false; _done = false; _needPick = true;
  }
  if (_needPick) pickItems();
  _lastDraw = now;
  const unsigned long rel = (now - _t0) / msPerPx();
  _lastStep = rel;

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);

  if (H > W) {
    // "IN SEASON" is wider than the portrait panel, so let it marquee above
    // one prominent piece of produce instead of squeezing the landscape strip.
    const char *title = "IN SEASON";
    const int titleW = (int)strlen(title) * 6 - 1;
    const int titleX = 1 - (int)(rel % (titleW + 10));
    gDisplay->setClipX(0, W);
    gDisplay->setTextColor(rgb565(_colTitle));
    gDisplay->setCursor(titleX, 0);
    gDisplay->print(title);
    gDisplay->setCursor(titleX + titleW + 10, 0);
    gDisplay->print(title);
    gDisplay->clearClip();
    if (_shownN == 0) { gDisplay->flipDMABuffer(); return; }

    const Produce &p = PRODUCE[_show[0]];
    const int s = 3;
    const int iconW = 9 * s;
    drawIcon(p, (W - iconW) / 2, H / 2 - iconW / 2, s, W);
    const int nameW = (int)strlen(p.name) * 6 - 1;
    const int nameX = nameW <= W - 2 ? (W - nameW) / 2 : 1 - (int)((rel / 2) % (nameW + 10));
    gDisplay->setClipX(0, W);
    gDisplay->setTextColor(rgb565(p.veg ? _colVeg : _colFruit));
    gDisplay->setCursor(nameX, H - 10);
    gDisplay->print(p.name);
    if (nameW > W - 2) { gDisplay->setCursor(nameX + nameW + 10, H - 10); gDisplay->print(p.name); }
    gDisplay->clearClip();
    if (rel > (unsigned long)(titleW + 10) * 3) _done = true;
    gDisplay->flipDMABuffer();
    return;
  }

  // title row
  gDisplay->setTextColor(rgb565(_colTitle));
  gDisplay->setCursor((W - 9 * 6) / 2, 0);                 // centered
  gDisplay->print("IN SEASON");

  if (_n == 0) { gDisplay->flipDMABuffer(); return; }
  const int s = (H - 8 >= 18) ? 2 : 1;                     // big pictures when there is room
  const int bandY = 8 + (H - 8 - 9 * s) / 2;
  _lap = 0;
  for (int i = 0; i < _shownN; i++) _lap += itemWidth(PRODUCE[_show[i]], s);
  if (_lap <= 0) return;

  int x = W - (int)rel;                                    // the list starts just off the right edge
  if (x + _lap <= 0) _done = true;                         // the last item has gone
  for (int i = 0; i < _shownN && x < W; i++) {
    const Produce &p = PRODUCE[_show[i]];
    const int w = itemWidth(p, s);
    if (x + w > 0) {
      drawIcon(p, x, bandY, s, W);
      gDisplay->setTextSize(s);
      gDisplay->setTextColor(rgb565(p.veg ? _colVeg : _colFruit));
      gDisplay->setCursor(x + 9 * s + 2 + s, bandY + s);
      gDisplay->print(p.name);
    }
    x += w;
  }
  gDisplay->setTextSize(1);
  gDisplay->flipDMABuffer();
}
