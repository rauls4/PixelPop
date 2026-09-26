#include "Calendar.h"
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

static const uint32_t DEF_BAND = 0xE0301E, DEF_BAND_TEXT = 0xFFFFFF, DEF_NOW = 0x1E9E4A;
static const uint32_t DESC_BG = 0xD9D9D9, DESC_TEXT = 0x000000;

static const unsigned long RETRY_MS = 60UL * 1000;               // after a failed download
static const unsigned long MAX_BYTES = 6UL * 1024 * 1024;        // a calendar bigger than this is cut off
static const unsigned long MAX_MS = 60UL * 1000;

static const int  DAYS_AHEAD[6] = {1, 2, 3, 7, 14, 30};
static const char *const DAYS_NAMES[6] = {"Today only", "2 days", "3 days", "A week", "2 weeks", "A month"};
static const unsigned EVERY_MIN[4] = {15, 30, 60, 120};
static const char *const EVERY_NAMES[4] = {"Every 15 minutes", "Every 30 minutes", "Every hour", "Every 2 hours"};
static const char *const USER_AGENT = "Mozilla/5.0 (compatible; PixelPop-ESP32)";

// ---------- settings ----------
void CalendarModule::onLoad() {
  _url        = store.getString("url", "");
  _daysIdx    = store.getU8("days", 3);
  if (_daysIdx > 5) _daysIdx = 3;
  _maxEvents  = store.getU8("max", 5);
  if (_maxEvents < 1 || _maxEvents > 10) _maxEvents = 5;
  _everyIdx   = store.getU8("ev", 1);
  if (_everyIdx > 3) _everyIdx = 1;
  _showAllDay = store.getBool("ad", true);
  _showNow    = store.getBool("now", true);
  _colBand     = store.getUInt("cBand", DEF_BAND);
  _colBandText = store.getUInt("cHead", DEF_BAND_TEXT);
  _colNow      = store.getUInt("cNow", DEF_NOW);
}

String CalendarModule::onSettingsHtml() {
  String h;
  h += uiSection("Calendar");
  h += uiText("url", "Published calendar address (starts with webcal:// or https://)", _url, 250);
  h += "<p class='m'>On a Mac: in the Calendar app, right-click the calendar, choose <b>Share Calendar</b>, "
       "tick <b>Public Calendar</b>, then copy the link. On iCloud.com or an iPhone: use the share button next to the "
       "calendar, switch on <b>Public Calendar</b> and copy the link. Anyone who has that link can read the calendar, "
       "so keep it to yourself. Calendars from Google, Outlook and others work too if you give their private .ics address.</p>";

  h += uiSection("Show");
  h += uiSelect("days", "Look ahead", DAYS_NAMES, 6, _daysIdx);
  h += uiNumber("max", "Number of events (each one gets a page)", _maxEvents, 1, 10);
  h += uiCheckbox("ad", "Include all-day events", _showAllDay);
  h += uiCheckbox("now", "Include events that have already started", _showNow);
  h += uiSelect("ev", "Update", EVERY_NAMES, 4, _everyIdx);
  h += "<p class='m'>Times are shown in the time zone chosen on the home page.</p>";

  h += uiSection("Colors");
  h += uiColor("cBand", "Band across the top", _colBand);
  h += uiColor("cHead", "Text in the band", _colBandText);
  h += uiColor("cNow", "Band for an event under way", _colNow);

  char st[96];
  if (_lock.take(100)) { strncpy(st, _status, sizeof(st) - 1); st[sizeof(st) - 1] = 0; _lock.give(); } else st[0] = 0;
  if (st[0]) h += "<p class='m'>Last update: " + htmlEscape(String(st)) + "</p>";
  return h;
}

void CalendarModule::onSave(WebServer &server) {
  String u = server.arg("url");
  u.trim();
  _url = u;
  _daysIdx    = (uint8_t)uiReadLong(server, "days", _daysIdx, 0, 5);
  _maxEvents  = (uint8_t)uiReadLong(server, "max", _maxEvents, 1, 10);
  _everyIdx   = (uint8_t)uiReadLong(server, "ev", _everyIdx, 0, 3);
  _showAllDay = server.hasArg("ad");
  _showNow    = server.hasArg("now");
  uint32_t c;
  if (uiReadColor(server, "cBand", c)) _colBand = c;
  if (uiReadColor(server, "cHead", c)) _colBandText = c;
  if (uiReadColor(server, "cNow", c))  _colNow = c;

  store.putString("url", _url);
  store.putU8("days", _daysIdx);
  store.putU8("max", _maxEvents);
  store.putU8("ev", _everyIdx);
  store.putBool("ad", _showAllDay);
  store.putBool("now", _showNow);
  store.putUInt("cBand", _colBand);
  store.putUInt("cHead", _colBandText);
  store.putUInt("cNow", _colNow);
  _fetchNow = true;                                  // new choices: fetch again right away
}

String CalendarModule::actionsHtml() {
  return "<form method='POST' action='/calendar/refresh'><button class='sec' type='submit'>Update the calendar now</button></form>"
         "<form method='POST' action='/calendar/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void CalendarModule::registerRoutes(WebServer &server) {
  server.on("/calendar/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/calendar");
  });
  server.on("/calendar/colors-reset", HTTP_POST, [this]() {
    _colBand = DEF_BAND; _colBandText = DEF_BAND_TEXT; _colNow = DEF_NOW;
    store.putUInt("cBand", _colBand);
    store.putUInt("cHead", _colBandText);
    store.putUInt("cNow", _colNow);
    app.requestRedraw();
    webRedirect("/calendar?saved=1");
  });
}

void CalendarModule::setStatus(const char *s) {
  if (_lock.take(200)) { strncpy(_status, s, sizeof(_status) - 1); _status[sizeof(_status) - 1] = 0; _lock.give(); }
}

// ---------- which events are on show ----------
int CalendarModule::visible(CalEvent *out, int max) {
  const ts_t now = (ts_t)time(nullptr);
  const ts_t horizon = now + (ts_t)DAYS_AHEAD[_daysIdx] * 86400;
  int n = 0;
  if (!_lock.take(50)) return 0;
  for (int i = 0; i < _evN && n < max; i++) {
    const CalEvent &e = _ev[i];
    const ts_t endEff = (e.end > e.start) ? e.end : e.start + 900;
    if (endEff <= now) continue;                     // over
    if (e.start >= horizon) continue;
    if (e.allDay && !_showAllDay) continue;
    if (e.start <= now && !_showNow) continue;
    out[n++] = e;
  }
  _lock.give();
  return n;
}

String CalendarModule::summary() {
  if (_url.length() == 0) return "Paste your calendar's public address on its page";
  if (!_tried) return "Loading the calendar";
  if (_failed) {
    char st[96] = "";
    if (_lock.take(50)) { strncpy(st, _status, sizeof(st) - 1); _lock.give(); }
    return String("Could not load: ") + st;
  }
  CalEvent v[10];
  int n = visible(v, _maxEvents);
  if (n == 0) return "No upcoming events";
  return String(n) + (n == 1 ? " event" : " events") + " - next: " + v[0].title;
}

// ---------- background task ----------
void CalendarModule::begin() {
  netWorkerAdd(this);                                   // downloads run in the shared network task
}

void CalendarModule::netTick() {
  if (millis() < 12000) return;                         // let the clock sync and the other modules settle
  if (!enabled() || _url.length() == 0) { _tried = false; return; }
  if (time(nullptr) < 1700000000) return;               // the clock has not synced yet
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

bool CalendarModule::fetch() {
  _parser = new (std::nothrow) IcsParser();
  if (!_parser) { setStatus("not enough memory"); return false; }
  const bool ok = fetchInner();
  delete _parser;
  _parser = nullptr;
  return ok;
}

bool CalendarModule::fetchInner() {
  String url = _url;
  url.trim();
  if (url.startsWith("webcal://")) url = "https://" + url.substring(9);
  else if (url.startsWith("webcals://")) url = "https://" + url.substring(10);
  if (!url.startsWith("https://") && !url.startsWith("http://")) { setStatus("the address must start with webcal:// or https://"); return false; }

  const ts_t now = (ts_t)time(nullptr);
  _parser->begin(now - 3600, now + (ts_t)DAYS_AHEAD[_daysIdx] * 86400 + 86400, timeZonePosix());

  WiFiClientSecure secure;
  secure.setInsecure();
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);                                 // plain, un-chunked reply we can read as a stream
  http.setUserAgent(USER_AGENT);
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  const bool ok = url.startsWith("https://") ? http.begin(secure, url) : http.begin(plain, url);
  if (!ok) { setStatus("could not open the address"); return false; }
  int status = http.GET();
  if (status != 200) {
    Serial.printf("Calendar: HTTP %d\n", status);
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
  snprintf(msg, sizeof(msg), "read %d events, %d coming up", _parser->eventsSeen(), _parser->count());
  setStatus(msg);
  Serial.printf("Calendar: %s (%lu bytes)\n", msg, total);
  return true;
}

// ---------- drawing ----------
int CalendarModule::pageCount() {
  CalEvent v[10];
  int n = visible(v, _maxEvents);
  if (gDisplay && gDisplay->height() > gDisplay->width()) return n > 0 ? (n + 1) / 2 : 1;
  return n > 0 ? n : 1;
}

bool CalendarModule::pageAvailable(int sub) {
  if (!_tried || _failed) return false;
  CalEvent v[10];
  const int n = visible(v, _maxEvents);
  return sub < ((gDisplay && gDisplay->height() > gDisplay->width()) ? (n + 1) / 2 : n);
}

bool CalendarModule::needsRedraw() {
  if (gDisplay && gDisplay->height() > gDisplay->width()) {
    const unsigned long step = millis() / 70;
    if (step != _lastScrollStep) { _lastScrollStep = step; return true; }
  }
  long m = (long)(time(nullptr) / 60);
  if (m == _lastMinute) return false;
  _lastMinute = m;
  return true;
}

static void printSmall(int x, int baseline, const char *s, uint16_t col) {
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(s);
  gDisplay->setFont(nullptr);
}

void CalendarModule::drawPage(int sub) {
  CalEvent v[10];
  const int n = visible(v, _maxEvents);
  const int W = gDisplay->width();
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  const bool portrait = gDisplay->height() > gDisplay->width();
  if (portrait) {
    const int H = gDisplay->height();
    const int first = sub * 2;
    CalZone board;
    if (!calZoneParse(timeZonePosix(), board)) calZoneParse("UTC0", board);
    for (int row = 0; row < 2; row++) {
      const int idx = first + row;
      if (idx >= n) break;
      const CalEvent &e = v[idx];
      const int y = row * H / 2;
      const int rowH = H / 2;
      char head[40];
      bool going = false;
      calHeader(e, (ts_t)time(nullptr), board, app.use24Hour(), head, sizeof(head), &going);
      gDisplay->fillRect(0, y, W, 7, rgb565(going ? _colNow : _colBand));
      gDisplay->setClipX(0, W);
      printSmall(1, y + 6, head, rgb565(_colBandText));
      gDisplay->clearClip();
      gDisplay->fillRect(0, y + 7, W, rowH - 7, rgb565(DESC_BG));

      const int titleW = (int)strlen(e.title) * 6 - 1;
      const int available = W - 2;
      int x = 1;
      if (titleW > available) {
        const int cycle = titleW + 10;
        const int offset = (millis() / 70 + row * 19) % cycle;
        x -= offset;
      } else x = (W - titleW) / 2;
      gDisplay->setClipX(0, W);
      gDisplay->setTextColor(rgb565(DESC_TEXT));
      const int titleY = y + 7 + (rowH - 7 - 8) / 2;
      gDisplay->setCursor(x, titleY);
      gDisplay->print(e.title);
      if (titleW > available) gDisplay->setCursor(x + titleW + 10, titleY), gDisplay->print(e.title);
      gDisplay->clearClip();
    }
  } else if (sub >= 0 && sub < n) {
    const CalEvent &e = v[sub];
    CalZone board;
    if (!calZoneParse(timeZonePosix(), board)) calZoneParse("UTC0", board);
    char head[40];
    bool going = false;
    calHeader(e, (ts_t)time(nullptr), board, app.use24Hour(), head, sizeof(head), &going);

    // top band: when
    gDisplay->fillRect(0, 0, W, 7, rgb565(going ? _colNow : _colBand));
    printSmall(1, 6, head, rgb565(_colBandText));
    gDisplay->fillRect(0, 7, W, gDisplay->height() - 7, rgb565(DESC_BG));

    // title: up to three lines, centered in the space below the band
    char lines[3][CAL_MAX_COLS + 1];
    const int cols = (W - 2) / 6;                    // 10 characters per line
    const int nl = calWrap(e.title, cols, 3, lines);
    const int H = gDisplay->height();
    const int y0 = 7 + ((H - 7) - (8 * nl - 1)) / 2;
    gDisplay->setTextColor(rgb565(DESC_TEXT));
    for (int i = 0; i < nl; i++) {
      const int w = (int)strlen(lines[i]) * 6 - 1;
      gDisplay->setCursor((W - w) / 2, y0 + i * 8);
      gDisplay->print(lines[i]);
    }
  }
  gDisplay->flipDMABuffer();
}
