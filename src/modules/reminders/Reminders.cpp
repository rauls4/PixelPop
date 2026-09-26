#include "Reminders.h"
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <Fonts/TomThumb.h>            // tiny 3x5 font for the band
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"

static const uint32_t DEF_OVER = 0xD62A2A, DEF_TODAY = 0xE08A00, DEF_BAND = 0x2F6FD6, DEF_TITLE = 0xFFFFFF, DEF_DETAIL = 0x9DB0CC;
static const int  WINDOW_DAYS[4] = {0, 3, 7, -1};
static const char *const WINDOW_NAMES[4] = {"Overdue and due today", "Due within 3 days", "Due within a week", "Everything"};
static const unsigned STALE_HOURS[5] = {0, 12, 24, 48, 168};
static const char *const STALE_NAMES[5] = {"Never", "12 hours", "24 hours", "2 days", "A week"};
static const unsigned long TEXT_PAUSE = 1500;                       // a long line holds still this long, then scrolls
static const unsigned long TEXT_STEP  = 50;                         // ms per pixel of scrolling
static const unsigned long BLINK_MS = 450;                          // the ! is on for this long, then off
static const size_t MAX_RAW = 3500;                               // what fits in one flash entry with room to spare

static uint32_t hashText(const String &s) {                       // ignores carriage returns (browsers add them)
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.length(); i++) { if (s[i] == '\r') continue; h ^= (unsigned char)s[i]; h *= 16777619u; }
  return h;
}

// ---------- settings ----------
void RemindersModule::onLoad() {
  // Reminders first used the flash prefix "r_", which the Radio function also uses. Carry the list, the
  // key and the settings over once to the prefix of its own (the key must stay the same so the shortcut
  // on the phone keeps working).
  if (store.getU8("mig", 0) == 0) {
    Store old("r_");
    const String k = old.getString("key", "");
    if (k.length()) {
      store.putString("key", k);
      store.putString("items", old.getString("items", ""));
      store.putU8("win", old.getU8("win", 3));
      store.putU8("max", old.getU8("max", 5));
      store.putU8("stale", old.getU8("stale", 3));
      store.putBool("und", old.getBool("und", true));
      store.putBool("h24", old.getBool("h24", false));
      store.putBool("done", old.getBool("done", true));
      store.putBool("got", old.getBool("got", false));
      store.putBool("uOver", old.getBool("uOver", false));
      store.putUInt("at", old.getUInt("at", 0));
      store.putUInt("cOver", old.getUInt("cOver", DEF_OVER));
      store.putUInt("cToday", old.getUInt("cToday", DEF_TODAY));
      store.putUInt("cBand", old.getUInt("cBand", DEF_BAND));
      store.putUInt("cTtl", old.getUInt("cTtl", DEF_TITLE));
      store.putUInt("cDet", old.getUInt("cDet", DEF_DETAIL));
    }
    store.putU8("mig", 1);
  }
  _key = store.getString("key", "");
  if (_key.length() == 0) {                          // first start: make a key
    char k[12];
    snprintf(k, sizeof(k), "%08x", (unsigned)esp_random());
    _key = k;
    store.putString("key", _key);
  }
  _window      = store.getU8("win", 3);
  if (_window > 3) _window = 3;
  _showUndated = store.getBool("und", true);
  _maxItems    = store.getU8("max", 5);
  if (_maxItems < 1 || _maxItems > 10) _maxItems = 5;
  _h24         = store.getBool("h24", false);
  _showDone    = store.getBool("done", true);
  _urgentOver  = store.getBool("uOver", false);
  _staleIdx    = store.getU8("stale", 3);
  if (_staleIdx > 4) _staleIdx = 3;
  _colOver     = store.getUInt("cOver", DEF_OVER);
  _colToday    = store.getUInt("cToday", DEF_TODAY);
  _colBand     = store.getUInt("cBand", DEF_BAND);
  _colTitle    = store.getUInt("cTtl", DEF_TITLE);
  _colDetail   = store.getUInt("cDet", DEF_DETAIL);
  _raw         = store.getString("items", "");
  _pushed      = store.getBool("got", false);
  _pushedAt    = store.getUInt("at", 0);
  _savedAt     = _pushedAt;
  _n = remParse(_raw.c_str(), _items, REM_MAX);
}

String RemindersModule::onSettingsHtml() {
  String h;
  h += uiSection("Getting your Apple reminders here");
  h += "<p class='m'>Apple does not let other devices read the Reminders app, so your iPhone or Mac sends the list to "
       "this board using the <b>Shortcuts</b> app. Make a shortcut that finds your incomplete reminders, joins them into "
       "text with one reminder per line, and sends that text to this address (as a form field named <b>items</b>):</p>";
  h += String("<p class='m'><b>http://") + HOSTNAME + ".local/reminders/push?key=" + htmlEscape(_key) +
       "</b><br>or, if that name does not work on your network: http://" + WiFi.localIP().toString() + "/reminders/push?key=" + htmlEscape(_key) + "</p>";
  h += "<p class='m'>Each line is the reminder, then bars and what you know about it: its due date, the list it is in, and a ! if it is "
       "high priority, like <b>Buy milk | 2026-09-21 15:30 | Groceries</b> or <b>Pay rent | 2026-09-22 | Home | !</b>. "
       "The order after the title does not matter, and every part is optional. "
       "To keep the board up to date, add a Shortcuts automation that runs the shortcut when the Reminders app is closed, "
       "or at set times. The step-by-step version is in the file <b>How to send Apple Reminders.md</b> in the sketch folder.</p>";

  h += uiSection("The list now");
  h += "<label>Reminders (one per line: title | due date | list | !). You can also type them here.</label>"
       "<textarea name='items' rows='7' style='width:100%;box-sizing:border-box;padding:12px;font-size:15px;border:1px solid #bbb;border-radius:8px'>" +
       htmlEscape(_raw) + "</textarea>";
  h += String("<input type='hidden' name='items0' value='") + String((unsigned long)hashText(_raw)) + "'>";
  if (_pushed) {
    time_t now = time(nullptr);
    String when = "";
    if (_pushedAt && now > 1700000000 && (uint32_t)now >= _pushedAt) {
      unsigned long mins = ((uint32_t)now - _pushedAt) / 60;
      when = mins < 60 ? String(mins) + " minutes ago" : mins < 2880 ? String(mins / 60) + " hours ago" : String(mins / 1440) + " days ago";
    }
    h += String("<p class='m'>") + String(_n) + (_n == 1 ? " reminder" : " reminders") + ", last updated " + (when.length() ? when : String("earlier")) + ".</p>";
  } else {
    h += "<p class='m'>Nothing has been sent yet.</p>";
  }

  h += uiSection("Show");
  h += uiSelect("win", "Which reminders", WINDOW_NAMES, 4, _window);
  h += uiCheckbox("und", "Include reminders with no due date", _showUndated);
  h += uiNumber("max", "Number of reminders (each one gets a page)", _maxItems, 1, 10);
  h += uiCheckbox("urgent", "Blink the ! for overdue reminders too (high priority ones always blink it)", _urgentOver);
  h += uiCheckbox("done", "Show an ALL CLEAR page when there is nothing to do", _showDone);
  h += uiCheckbox("h24", "24-hour times (3:30 PM becomes 15:30)", _h24);
  h += uiSelect("stale", "Hide the list if it has not been updated for", STALE_NAMES, 5, _staleIdx);

  h += uiSection("Colors");
  h += uiColor("cOver", "Band for overdue", _colOver);
  h += uiColor("cToday", "Band for due today", _colToday);
  h += uiColor("cBand", "Band for later or no date", _colBand);
  h += uiColor("cTtl", "Reminder title", _colTitle);
  h += uiColor("cDet", "Date and list line", _colDetail);
  return h;
}

void RemindersModule::onSave(WebServer &server) {
  _window      = (uint8_t)uiReadLong(server, "win", _window, 0, 3);
  _showUndated = server.hasArg("und");
  _maxItems    = (uint8_t)uiReadLong(server, "max", _maxItems, 1, 10);
  _showDone    = server.hasArg("done");
  _urgentOver  = server.hasArg("urgent");
  _h24         = server.hasArg("h24");
  _staleIdx    = (uint8_t)uiReadLong(server, "stale", _staleIdx, 0, 4);
  uint32_t c;
  if (uiReadColor(server, "cOver", c))  _colOver = c;
  if (uiReadColor(server, "cToday", c)) _colToday = c;
  if (uiReadColor(server, "cBand", c))  _colBand = c;
  if (uiReadColor(server, "cTtl", c))   _colTitle = c;
  if (uiReadColor(server, "cDet", c))   _colDetail = c;
  store.putU8("win", _window);
  store.putBool("und", _showUndated);
  store.putU8("max", _maxItems);
  store.putBool("done", _showDone);
  store.putBool("uOver", _urgentOver);
  store.putBool("h24", _h24);
  store.putU8("stale", _staleIdx);
  store.putUInt("cOver", _colOver);
  store.putUInt("cToday", _colToday);
  store.putUInt("cBand", _colBand);
  store.putUInt("cTtl", _colTitle);
  store.putUInt("cDet", _colDetail);
  // the text box only counts if it was changed here (a list sent from the phone in the meantime must not be overwritten)
  if (server.hasArg("items") && hashText(server.arg("items")) != (uint32_t)strtoul(server.arg("items0").c_str(), nullptr, 10))
    setItems(server.arg("items"));
}

String RemindersModule::actionsHtml() {
  return "<form method='POST' action='/reminders/clear'><button class='sec' type='submit'>Clear the list</button></form>"
         "<form method='POST' action='/reminders/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void RemindersModule::registerRoutes(WebServer &server) {
  // The Shortcuts app posts here.
  server.on("/reminders/push", HTTP_ANY, [this, &server]() {
    if (server.arg("key") != _key) { server.send(403, "text/plain", "Wrong or missing key\n"); return; }
    if (!server.hasArg("items") && !server.hasArg("plain")) { server.send(400, "text/plain", "Send the list in a field named items\n"); return; }
    setItems(server.hasArg("items") ? server.arg("items") : server.arg("plain"));
    server.send(200, "text/plain", String("OK, ") + _n + " reminders\n");
  });
  server.on("/reminders/clear", HTTP_POST, [this]() {
    setItems("");
    webRedirect("/reminders?saved=1");
  });
  server.on("/reminders/colors-reset", HTTP_POST, [this]() {
    _colOver = DEF_OVER; _colToday = DEF_TODAY; _colBand = DEF_BAND; _colTitle = DEF_TITLE; _colDetail = DEF_DETAIL;
    store.putUInt("cOver", _colOver);
    store.putUInt("cToday", _colToday);
    store.putUInt("cBand", _colBand);
    store.putUInt("cTtl", _colTitle);
    store.putUInt("cDet", _colDetail);
    app.requestRedraw();
    webRedirect("/reminders?saved=1");
  });
}

// A new list arrived (from the phone or typed in): keep it, remember when, save it.
void RemindersModule::setItems(const String &text) {
  String t;
  t.reserve(text.length());
  for (size_t i = 0; i < text.length() && t.length() < MAX_RAW; i++) if (text[i] != '\r') t += text[i];
  const bool changed = (t != _raw);
  _raw = t;
  _n = remParse(_raw.c_str(), _items, REM_MAX);
  _pushed = true;
  time_t now = time(nullptr);
  _pushedAt = (now > 1700000000) ? (uint32_t)now : 0;
  if (changed) { store.putString("items", _raw); store.putBool("got", true); store.putUInt("at", _pushedAt); _savedAt = _pushedAt; }
  else if (_pushedAt >= _savedAt + 6 * 3600) { store.putUInt("at", _pushedAt); _savedAt = _pushedAt; }   // spare the flash
  app.requestRedraw();
}

// ---------- which reminders are on show ----------
bool RemindersModule::stale() const {
  if (STALE_HOURS[_staleIdx] == 0 || _pushedAt == 0) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  return (uint32_t)now > _pushedAt + STALE_HOURS[_staleIdx] * 3600UL;
}

ts_t RemindersModule::nowWall() const {
  const ts_t now = (ts_t)time(nullptr);
  if (now < 1700000000) return 0;
  CalZone z;
  if (!calZoneParse(timeZonePosix(), z)) calZoneParse("UTC0", z);
  return now + calZoneOffsetUtc(z, now);
}

int RemindersModule::visible(RemItem *out, int max) {
  const ts_t now = nowWall();
  if (now == 0) return 0;
  int order[10];
  const int m = remOrder(_items, _n, now, WINDOW_DAYS[_window], _showUndated, order, max > 10 ? 10 : max);
  for (int i = 0; i < m; i++) out[i] = _items[order[i]];
  return m;
}

String RemindersModule::summary() {
  if (!_pushed) return "Nothing sent yet - see the Reminders page for how to send them";
  if (stale()) return "The list is out of date - send it again from Shortcuts";
  RemItem v[10];
  const int n = visible(v, 10);
  const ts_t now = nowWall();
  int over = 0;
  for (int i = 0; i < n; i++) if (now && remState(v[i], now) == REM_OVERDUE) over++;
  if (_n == 0) return "Nothing to do";
  String s = String(_n) + (_n == 1 ? " reminder" : " reminders");
  if (over) s += String(", ") + over + " overdue";
  return s;
}

// ---------- drawing ----------
int RemindersModule::pageCount() {
  RemItem v[10];
  const int n = visible(v, _maxItems);
  return n > 0 ? n : 1;
}

bool RemindersModule::pageAvailable(int sub) {
  if (!_pushed || stale() || nowWall() == 0) return false;
  RemItem v[10];
  const int n = visible(v, _maxItems);
  if (n > 0) return sub < n;
  return _showDone && sub == 0;
}

bool RemindersModule::needsRedraw() {
  const unsigned long now = millis();
  if (_scrolling && (now - _scrollT0) / TEXT_STEP != _lastFrame) return true;     // the text moved a pixel
  if (_blinking && (long)(now / BLINK_MS) != _lastPhase) return true;
  const long m = (long)(time(nullptr) / 60);
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

void RemindersModule::drawDone() {
  const int W = gDisplay->width(), H = gDisplay->height();
  const uint16_t green = rgb565(0x30E060);
  const int x0 = W / 2 - 7, y0 = 2;                  // a check mark, two pixels thick
  for (int t = 0; t < 2; t++) {
    gDisplay->drawLine(x0, y0 + 6 + t, x0 + 4, y0 + 10 + t, green);
    gDisplay->drawLine(x0 + 4, y0 + 10 + t, x0 + 13, y0 + 1 + t, green);
  }
  const char *msg = "ALL CLEAR";
  const int w = (int)strlen(msg) * 6 - 1;
  gDisplay->setTextColor(rgb565(_colTitle));
  gDisplay->setCursor((W - w) / 2, H - 12);
  gDisplay->print(msg);
}

// One line of text inside the columns x0 <= x < x1: centered if it fits, otherwise it holds still for a
// moment and then scrolls sideways, round and round. "small" is the tiny font (y = top of the letters).
void RemindersModule::field(const char *s, int x0, int x1, int y, bool small, uint16_t col, unsigned long now) {
  const int adv = small ? 4 : 6;
  const int tw = (int)strlen(s) * adv - 1;
  const int aw = x1 - x0;
  if (small) gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  const int cy = small ? y + 5 : y;                 // the tiny font is placed by its baseline
  if (tw <= aw) {
    gDisplay->setCursor(x0 + (aw - tw) / 2, cy);
    gDisplay->print(s);
  } else {
    _scrolling = true;
    const int period = tw + 12;
    const unsigned long t = (now - _scrollT0) % (TEXT_PAUSE + (unsigned long)period * TEXT_STEP);
    const int off = (t < TEXT_PAUSE) ? 0 : (int)((t - TEXT_PAUSE) / TEXT_STEP);
    gDisplay->setClipX(x0, x1);
    gDisplay->setCursor(x0 - off, cy);
    gDisplay->print(s);
    gDisplay->setCursor(x0 - off + period, cy);
    gDisplay->print(s);
    gDisplay->clearClip();
  }
  if (small) gDisplay->setFont(nullptr);
}

void RemindersModule::drawItem(const RemItem &r, ts_t wallNow) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  char head[32];
  const RemState st = remHeader(r, wallNow, _h24, head, sizeof(head));
  const uint32_t band = (st == REM_OVERDUE) ? _colOver : (st == REM_TODAY) ? _colToday : _colBand;
  const bool urgent = r.high || (_urgentOver && st == REM_OVERDUE);
  gDisplay->fillRect(0, 0, W, 7, rgb565(band));
  // an empty round checkbox
  const uint16_t white = COL_WHITE;
  static const uint8_t ring[5] = {0x0E, 0x11, 0x11, 0x11, 0x0E};
  for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) if (ring[y] & (0x10 >> x)) gDisplay->drawPixel(1 + x, 1 + y, white);
  if (urgent) gDisplay->setClipX(0, W - 6);          // keep the text off the !
  printSmall(8, 6, head, white);
  gDisplay->clearClip();
  if (urgent) {
    _blinking = true;
    if (((now / BLINK_MS) & 1) == 0) {               // a bold exclamation mark: bar, gap, dot
      for (int y = 1; y <= 3; y++) { gDisplay->drawPixel(W - 4, y, white); gDisplay->drawPixel(W - 3, y, white); }
      gDisplay->drawPixel(W - 4, 5, white); gDisplay->drawPixel(W - 3, 5, white);
    }
  }

  // the title (one line that scrolls if it is long) and the small line under it
  char detail[64];
  remDetail(r, _h24, detail, sizeof(detail));
  const bool hasDetail = detail[0] != 0;
  int titleY, detailY = 0;
  if (hasDetail) {
    const int free = (H - 7) - 12;                   // 7 rows for the title, 5 for the small line
    const int gap = free > 3 ? free / 3 : 1;
    titleY = 7 + gap + (free - 3 * gap) / 2;
    detailY = titleY + 7 + gap;
  } else {
    titleY = 7 + ((H - 7) - 7) / 2;
  }
  field(r.title, 1, W - 1, titleY, false, rgb565(_colTitle), now);
  if (hasDetail) field(detail, 1, W - 1, detailY, true, rgb565(_colDetail), now);
}

void RemindersModule::drawPage(int sub) {
  RemItem v[10];
  const int n = visible(v, _maxItems);
  const unsigned long now = millis();
  if (now - _lastDraw > 500 || sub != _lastSub) _scrollT0 = now;      // a fresh showing: scroll from the start
  _lastDraw = now;
  _lastFrame = (now - _scrollT0) / TEXT_STEP;         // the scroll step on screen
  _lastSub = sub;
  _scrolling = _blinking = false;
  _lastPhase = (long)(now / BLINK_MS);
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  if (n > 0 && sub >= 0 && sub < n) drawItem(v[sub], nowWall());
  else if (n == 0 && _showDone) drawDone();
  gDisplay->flipDMABuffer();
}
