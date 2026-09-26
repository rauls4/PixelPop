#include "Menu.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <new>
#include <string.h>
#include <Fonts/TomThumb.h>            // tiny 3x5 font for the top band
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../ticker/TickerData.h"      // Dechunker

static const uint32_t DEF_BAND = 0x8A3FD0, DEF_BAND_TEXT = 0xFFFFFF, DEF_TODAY = 0xE0301E, DEF_A = 0xFFFFFF, DEF_B = 0xFFD060;

static const unsigned long RETRY_MS = 60UL * 1000;
static const unsigned long MAX_BYTES = 6UL * 1024 * 1024;
static const unsigned long MAX_MS = 60UL * 1000;
static const unsigned long HOLD_START_MS = 2500;       // a long menu waits at the top before it scrolls
static const unsigned long HOLD_END_MS = 2000;         // and at the bottom before the page moves on

static const char *const SPAN_NAMES[5] = {"Today only", "Today and tomorrow", "The next 3 days", "The rest of this week (to Sunday)", "The next 7 days"};
static const unsigned EVERY_MIN[4] = {60, 180, 360, 720};
static const char *const EVERY_NAMES[4] = {"Every hour", "Every 3 hours", "Every 6 hours", "Every 12 hours"};
static const char *const SPEED_NAMES[3] = {"Slow", "Normal", "Fast"};
static const char *const USER_AGENT = "Mozilla/5.0 (compatible; PixelPop-ESP32)";
static const char *const WDAY[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static ts_t fdiv(ts_t a, ts_t b) {
  ts_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

// ---------- settings ----------
void MenuModule::onLoad() {
  _url       = store.getString("url", "");
  _spanIdx   = store.getU8("span", 4);
  if (_spanIdx > 4) _spanIdx = 4;
  _everyIdx  = store.getU8("ev", 1);
  if (_everyIdx > 3) _everyIdx = 1;
  _speed     = store.getU8("spd", 1);
  if (_speed > 2) _speed = 1;
  _colBand     = store.getUInt("cBand", DEF_BAND);
  _colBandText = store.getUInt("cHead", DEF_BAND_TEXT);
  _colToday    = store.getUInt("cTod", DEF_TODAY);
  _colA        = store.getUInt("cA", DEF_A);
  _colB        = store.getUInt("cB", DEF_B);
}

String MenuModule::onSettingsHtml() {
  String h;
  h += uiSection("Menu calendar");
  h += uiText("url", "Calendar address (starts with webcal:// or https://)", _url, 250);
  h += "<p class='m'>Keep the menu in a calendar: one event per dish or per meal, on the day it is served. "
       "The title of the event is what appears on the panel, so \"Lasagna and salad\" is shown as written. "
       "Whole-day events and timed ones both work, and so do repeating events (for example a Friday pizza). "
       "Then share that calendar as a <b>Public Calendar</b> (right-click it in the Calendar app on a Mac, or use the "
       "share button on iCloud.com or an iPhone) and paste its link here. Google and Outlook calendars work with their "
       "private .ics address. Anyone who has the link can read the calendar, so keep it to yourself. "
       "The panel remembers up to 24 events, which is a week of three meals a day.</p>";

  h += uiSection("Show");
  h += uiSelect("span", "Days to show", SPAN_NAMES, 5, _spanIdx);
  h += uiSelect("spd", "Scroll speed for a long menu", SPEED_NAMES, 3, _speed);
  h += uiSelect("ev", "Update", EVERY_NAMES, 4, _everyIdx);
  h += "<p class='m'>A day with no menu is skipped. Days are worked out in the time zone chosen on the home page.</p>";

  h += uiSection("Colors");
  h += uiColor("cBand", "Band across the top", _colBand);
  h += uiColor("cTod", "Band for today", _colToday);
  h += uiColor("cHead", "Text in the band", _colBandText);
  h += uiColor("cA", "Dishes (odd ones)", _colA);
  h += uiColor("cB", "Dishes (even ones)", _colB);

  char st[96];
  if (_lock.take(100)) { strncpy(st, _status, sizeof(st) - 1); st[sizeof(st) - 1] = 0; _lock.give(); } else st[0] = 0;
  if (st[0]) h += "<p class='m'>Last update: " + htmlEscape(String(st)) + "</p>";
  return h;
}

void MenuModule::onSave(WebServer &server) {
  String u = server.arg("url");
  u.trim();
  _url = u;
  _spanIdx  = (uint8_t)uiReadLong(server, "span", _spanIdx, 0, 4);
  _speed    = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _everyIdx = (uint8_t)uiReadLong(server, "ev", _everyIdx, 0, 3);
  uint32_t c;
  if (uiReadColor(server, "cBand", c)) _colBand = c;
  if (uiReadColor(server, "cTod", c))  _colToday = c;
  if (uiReadColor(server, "cHead", c)) _colBandText = c;
  if (uiReadColor(server, "cA", c))    _colA = c;
  if (uiReadColor(server, "cB", c))    _colB = c;

  store.putString("url", _url);
  store.putU8("span", _spanIdx);
  store.putU8("spd", _speed);
  store.putU8("ev", _everyIdx);
  store.putUInt("cBand", _colBand);
  store.putUInt("cTod", _colToday);
  store.putUInt("cHead", _colBandText);
  store.putUInt("cA", _colA);
  store.putUInt("cB", _colB);
  _fetchNow = true;
  _lastOffset = -1;
}

String MenuModule::actionsHtml() {
  return "<form method='POST' action='/menu/refresh'><button class='sec' type='submit'>Update the menu now</button></form>"
         "<form method='POST' action='/menu/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void MenuModule::registerRoutes(WebServer &server) {
  server.on("/menu/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/menu");
  });
  server.on("/menu/colors-reset", HTTP_POST, [this]() {
    _colBand = DEF_BAND; _colBandText = DEF_BAND_TEXT; _colToday = DEF_TODAY; _colA = DEF_A; _colB = DEF_B;
    store.putUInt("cBand", _colBand);
    store.putUInt("cHead", _colBandText);
    store.putUInt("cTod", _colToday);
    store.putUInt("cA", _colA);
    store.putUInt("cB", _colB);
    app.requestRedraw();
    webRedirect("/menu?saved=1");
  });
}

void MenuModule::setStatus(const char *s) {
  if (_lock.take(200)) { strncpy(_status, s, sizeof(_status) - 1); _status[sizeof(_status) - 1] = 0; _lock.give(); }
}

// ---------- which days are on show ----------
// How many days from today, counting today.
int MenuModule::spanDays(long today) const {
  switch (_spanIdx) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    case 3: {                                        // up to and including Sunday
      const int wd = calWeekday(today);              // 0 = Sunday
      return ((7 - wd) % 7) + 1;
    }
    default: return 7;
  }
}

int MenuModule::scan(int want, DayView *out) {
  CalZone z;
  if (!calZoneParse(timeZonePosix(), z)) calZoneParse("UTC0", z);
  const ts_t now = (ts_t)time(nullptr);
  const ts_t today = fdiv(now + calZoneOffsetUtc(z, now), 86400);
  const int span = spanDays((long)today);
  int found = 0;
  if (!_lock.take(50)) return 0;
  for (int d = 0; d < span; d++) {
    const ts_t lo = (today + d) * 86400, hi = lo + 86400;      // this local day, as local seconds
    const bool fill = out && found == want;
    int n = 0;
    for (int i = 0; i < _evN; i++) {
      const CalEvent &e = _ev[i];
      if (!e.title[0]) continue;
      const ts_t s = e.start + calZoneOffsetUtc(z, e.start);
      ts_t en = e.end + calZoneOffsetUtc(z, e.end);
      if (en <= s) en = s + 1;
      if (s >= hi || en <= lo) continue;
      if (n >= MAXPER) break;
      if (fill) { strncpy(out->item[n], e.title, CAL_TITLE_LEN - 1); out->item[n][CAL_TITLE_LEN - 1] = 0; }
      n++;
    }
    if (n > 0) {
      if (fill) { out->n = n; out->day = (long)(today + d); out->ahead = d; }
      found++;
    }
  }
  _lock.give();
  return found;
}

String MenuModule::summary() {
  if (_url.length() == 0) return "Paste the menu calendar's public address on its page";
  if (!_tried) return "Loading the menu";
  if (_failed) {
    char st[96] = "";
    if (_lock.take(50)) { strncpy(st, _status, sizeof(st) - 1); _lock.give(); }
    return String("Could not load: ") + st;
  }
  DayView dv;
  const int n = scan(0, &dv);
  if (n == 0) return "No menu for the coming days";
  return String(n) + (n == 1 ? " day" : " days") + " with a menu - first: " + dv.item[0];
}

// ---------- background task ----------
void MenuModule::begin() {
  netWorkerAdd(this);                                   // downloads run in the shared network task
}

void MenuModule::netTick() {
  if (millis() < 14000) return;                         // let the clock sync and the other modules settle
  if (!enabled() || _url.length() == 0) { _tried = false; return; }
  if (time(nullptr) < 1700000000) return;
  const bool force = _fetchNow;
  _fetchNow = false;
  const unsigned long interval = _failed ? RETRY_MS : EVERY_MIN[_everyIdx] * 60000UL;
  if (force || !_tried || millis() - _lastTry >= interval) {
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED && netLockTake(120000)) {
      ok = fetch();
      netLockGive();
    } else setStatus("waiting for the network");
    _failed = !ok;
    _tried = true;
    _lastTry = millis();
    app.requestRedraw();
  }
}

bool MenuModule::fetch() {
  _parser = new (std::nothrow) IcsParser();
  if (!_parser) { setStatus("not enough memory"); return false; }
  const bool ok = fetchInner();
  delete _parser;
  _parser = nullptr;
  return ok;
}

bool MenuModule::fetchInner() {
  String url = _url;
  url.trim();
  if (url.startsWith("webcal://")) url = "https://" + url.substring(9);
  else if (url.startsWith("webcals://")) url = "https://" + url.substring(10);
  if (!url.startsWith("https://") && !url.startsWith("http://")) { setStatus("the address must start with webcal:// or https://"); return false; }

  // The window starts at the beginning of today (local time), so today's dishes count even after lunch.
  CalZone z;
  if (!calZoneParse(timeZonePosix(), z)) calZoneParse("UTC0", z);
  const ts_t now = (ts_t)time(nullptr);
  const ts_t todayLocal = fdiv(now + calZoneOffsetUtc(z, now), 86400) * 86400;
  const ts_t lo = calZoneLocalToUtc(z, todayLocal);
  _parser->begin(lo, lo + 9 * 86400, timeZonePosix());

  WiFiClientSecure secure;
  secure.setInsecure();
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  http.setUserAgent(USER_AGENT);
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  const bool ok = url.startsWith("https://") ? http.begin(secure, url) : http.begin(plain, url);
  if (!ok) { setStatus("could not open the address"); return false; }
  int status = http.GET();
  if (status != 200) {
    Serial.printf("Menu: HTTP %d\n", status);
    char msg[48];
    snprintf(msg, sizeof(msg), "the server answered %d", status);
    setStatus(status > 0 ? msg : "could not connect");
    http.end();
    return false;
  }

  const bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  Dechunker dc;
  dc.begin();
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[512];
  unsigned long total = 0, start = millis(), lastData = start;
  while (s && total < MAX_BYTES && millis() - start < MAX_MS) {
    int avail = s->available();
    if (avail > 0) {
      int n = s->read(buf, avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail);
      for (int i = 0; i < n; i++)
        if (!chunked || dc.feed((char)buf[i])) _parser->feed((char)buf[i]);
      total += n;
      lastData = millis();
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      if (!s->connected()) break;
      if (millis() - lastData > 8000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  _parser->finish();
  if (!_parser->sawCalendar()) { setStatus("that address did not give a calendar"); return false; }

  if (_lock.take(1000)) {
    _evN = _parser->count();
    for (int i = 0; i < _evN; i++) _ev[i] = _parser->event(i);
    _lock.give();
  }
  char msg[80];
  snprintf(msg, sizeof(msg), "read %d events, %d in the coming days", _parser->eventsSeen(), _parser->count());
  setStatus(msg);
  Serial.printf("Menu: %s (%lu bytes)\n", msg, total);
  return true;
}

// ---------- drawing ----------
int MenuModule::pageCount() {
  const int n = scan(-1, nullptr);
  return n > 0 ? n : 1;
}

bool MenuModule::pageAvailable(int sub) {
  if (!_tried || _failed) return false;
  return sub < scan(-1, nullptr);
}

unsigned long MenuModule::msPerPx() const {
  static const unsigned long MS[3] = {95, 60, 35};
  return MS[_speed > 2 ? 1 : _speed];
}

static int offsetAt(unsigned long t, int dist, unsigned long step) {
  if (t < HOLD_START_MS) return 0;
  unsigned long o = (t - HOLD_START_MS) / step;
  return o > (unsigned long)dist ? dist : (int)o;
}

bool MenuModule::needsRedraw() {
  const unsigned long now = millis();
  if (now - _lastSeen > 500) { _t0 = now; _lastOffset = -1; }       // it was not on the screen: a new showing
  _lastSeen = now;
  if (_scrollable) {
    if (now - _t0 >= _totalMs + 300) { _t0 = now; return true; }      // read to the end: start again (a single page)
    const int o = offsetAt(now - _t0, _scrollDist, _stepMs);
    return o != _lastOffset;
  }
  const long m = (long)(time(nullptr) / 60);                         // "TODAY" becomes "TOMORROW" at midnight
  if (m == _lastMinute && _lastOffset != -1) return false;
  _lastMinute = m;
  return true;
}

int MenuModule::pageProgress() {
  const unsigned long now = millis();
  if (!_scrollable) return 0;                                        // it fits: the rotation time applies
  if (now - _lastSeen > 1000) return 0;                              // not on the screen yet
  return (now - _t0 >= _totalMs) ? 2 : 1;
}

static void printSmall(int x, int baseline, const char *s, uint16_t col) {
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(s);
  gDisplay->setFont(nullptr);
}

void MenuModule::drawPage(int sub) {
  DayView dv;
  const int days = scan(sub, &dv);
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  if (sub != _curSub) { _curSub = sub; _t0 = now; }
  _lastSeen = now;
  _lastOffset = 0;
  _scrollable = false;

  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  if (sub < 0 || sub >= days || dv.n == 0) { gDisplay->flipDMABuffer(); return; }

  // ---- lay the dishes out: each is wrapped to the width, with a little gap between dishes ----
  static const int MAXL = 24;
  char lines[MAXL][CAL_MAX_COLS + 1];
  uint8_t which[MAXL];
  int lineY[MAXL];
  int nl = 0, y = 0;
  const int cols = (W - 2) / 6;
  for (int i = 0; i < dv.n && nl < MAXL; i++) {
    char w[4][CAL_MAX_COLS + 1];
    const int k = calWrap(dv.item[i], cols, 4, w);
    if (i > 0) y += 2;
    for (int j = 0; j < k && nl < MAXL; j++) {
      memcpy(lines[nl], w[j], CAL_MAX_COLS + 1);
      which[nl] = (uint8_t)i;
      lineY[nl] = y;
      nl++;
      y += 8;
    }
  }
  const int contentH = y > 0 ? y - 1 : 0;
  const int bodyTop = 7, bodyH = H - bodyTop;
  int y0;
  if (contentH <= bodyH) {
    y0 = bodyTop + (bodyH - contentH) / 2;
  } else {
    _scrollable = true;
    _stepMs = msPerPx();
    _scrollDist = contentH - (H - 8);                                // the last line ends at the bottom edge
    _totalMs = HOLD_START_MS + (unsigned long)_scrollDist * _stepMs + HOLD_END_MS;
    const int off = offsetAt(now - _t0, _scrollDist, _stepMs);
    _lastOffset = off;
    y0 = 8 - off;
  }
  const bool centered = !_scrollable;
  for (int i = 0; i < nl; i++) {
    const int yy = y0 + lineY[i];
    if (yy + 7 <= 0 || yy >= H) continue;
    gDisplay->setTextColor(rgb565((which[i] & 1) ? _colB : _colA));
    const int w = (int)strlen(lines[i]) * 6 - 1;
    gDisplay->setCursor(centered ? (W - w) / 2 : 1, yy);
    gDisplay->print(lines[i]);
  }

  // ---- top band, drawn last so a scrolling menu slides under it ----
  const bool today = dv.ahead == 0;
  gDisplay->fillRect(0, 0, W, 7, rgb565(today ? _colToday : _colBand));
  char label[24];
  if (today) snprintf(label, sizeof(label), "TODAY");
  else if (dv.ahead == 1) snprintf(label, sizeof(label), "TOMORROW");
  else {
    CalTm t;
    calBreak((ts_t)dv.day * 86400, t);
    snprintf(label, sizeof(label), "%s %s %d", WDAY[t.wday % 7], MON[(t.mon - 1 + 12) % 12], t.day);
  }
  printSmall(1, 6, label, rgb565(_colBandText));
  const int wm = 4 * 4;                                              // "MENU" in the TomThumb font
  printSmall(W - wm, 6, "MENU", rgb565(_colBandText));
  gDisplay->flipDMABuffer();
}
