#include "Flight.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../../core/NetLock.h"
#include "Logos.h"
#include <Fonts/TomThumb.h>            // tiny 3x5 font for speed and distance

static const unsigned long SCROLL_STEP = 70;      // ms per pixel of scrolling

static const uint32_t DEF_COL_CALL   = 0xFFD200;
static const uint32_t DEF_COL_DATA   = 0xFFFFFF;
static const uint32_t DEF_COL_ACCENT = 0x00C8FF;

static const unsigned long RETRY_MS = 20UL * 1000;       // after a failed fetch
static const unsigned long STALE_MS = 3UL * 60 * 1000;   // stop showing data older than this

// ---------- settings ----------
void FlightModule::onLoad() {
  _apt = store.getU8("apt", 0);
  if (_apt > airportCount()) _apt = 0;
  _lat = store.getFloat("lat", 41.8745f);
  _lon = store.getFloat("lon", -87.6512f);
  _radius = store.getU8("rad", 25);
  if (_radius < 5) _radius = 5;
  if (_radius > 100) _radius = 100;
  _maxShown = store.getU8("max", 8);
  if (_maxShown < 1) _maxShown = 1;
  if (_maxShown > AircraftList::CAP) _maxShown = AircraftList::CAP;
  _hideGround = store.getBool("gnd", true);
  _majorOnly  = store.getBool("maj", true);
  _showEmpty  = store.getBool("emp", false);
  _units = store.getU8("uni", 0);
  if (_units > 2) _units = 0;
  _poll = store.getU8("pol", 30);
  _showLogos = store.getBool("lgo", true);
  _showStatus = store.getBool("sta", true);
  _showNew = store.getBool("new", true);
  _logoBright = store.getU8("lgb", 70);
  if (_logoBright < 10) _logoBright = 10;
  if (_logoBright > 100) _logoBright = 100;
  if (_poll != 15 && _poll != 30 && _poll != 60) _poll = 30;
  _colCall   = store.getUInt("cCall", DEF_COL_CALL);
  _colData   = store.getUInt("cData", DEF_COL_DATA);
  _colAccent = store.getUInt("cAcc", DEF_COL_ACCENT);
}

String FlightModule::onSettingsHtml() {
  String h;
  h += uiSection("Where to look");

  // option 0 = custom coordinates, then the airports
  static char labels[40][40];
  static const char *opts[40];
  int n = airportCount() + 1;
  if (n > 40) n = 40;
  snprintf(labels[0], sizeof(labels[0]), "My own coordinates (below)");
  opts[0] = labels[0];
  for (int i = 1; i < n; i++) {
    const Airport &a = airportAt(i - 1);
    snprintf(labels[i], sizeof(labels[i]), "%s - %s", a.code, a.name);
    opts[i] = labels[i];
  }
  h += uiSelect("apt", "Center on", opts, n, _apt);
  h += "<label>Latitude (used with my own coordinates)</label><input type='number' name='lat' step='any' min='-90' max='90' value='" +
       String(_lat, 4) + "'>";
  h += "<label>Longitude (used with my own coordinates)</label><input type='number' name='lon' step='any' min='-180' max='180' value='" +
       String(_lon, 4) + "'>";
  h += uiNumber("rad", "Search radius in nautical miles (5 to 100)", _radius, 5, 100);

  h += uiSection("What to show");
  h += uiNumber("max", "Most aircraft per round (1 to 12, nearest first)", _maxShown, 1, AircraftList::CAP);
  h += uiCheckbox("maj", "Only U.S. major passenger airlines", _majorOnly);
  h += uiCheckbox("gnd", "Hide aircraft that are on the ground", _hideGround);
  h += uiCheckbox("emp", "Show a \"no flights\" screen when nothing is in range", _showEmpty);
  h += uiCheckbox("lgo", "Show the airline logo at the top left", _showLogos);
  h += uiCheckbox("new", "Show a red corner tag on flights that have just appeared", _showNew);
  h += uiCheckbox("sta", "Show the flight status (climb, descent, cruise, emergency) beside the altitude", _showStatus);
  h += uiNumber("lgb", "Logo brightness, percent (10 to 100)", _logoBright, 10, 100);
  static const char *const units[] = {"Aviation: feet / flight level, knots, nautical miles",
                                      "US: feet, mph, miles",
                                      "Metric: meters, km/h, km"};
  h += uiSelect("uni", "Units", units, 3, _units);
  static const char *const polls[] = {"Every 15 seconds", "Every 30 seconds", "Every minute"};
  h += uiSelect("pol", "Update the list", polls, 3, _poll == 15 ? 0 : (_poll == 60 ? 2 : 1));

  h += uiSection("Colors");
  h += uiColor("cCall", "Callsign", _colCall);
  h += uiColor("cData", "Flight data", _colData);
  h += uiColor("cAcc", "Distance and small labels", _colAccent);

  h += "<p class='m'>Flight positions come from adsb.lol, a free community ADS-B network. "
       "It shows what receivers nearby can hear. The major-airline filter matches American, Delta, United, Southwest, "
       "JetBlue, Alaska, Spirit, Frontier, Hawaiian, and Allegiant by their callsign. "
       "It does not include where a flight is coming from or going to. "
       "Airline logos appear for flights whose callsign starts with the airline's 3-letter code (UAL1234, DAL88, ...). "
       "The altitude turns green while an aircraft climbs and orange while it descends.</p>";
  return h;
}

void FlightModule::onSave(WebServer &server) {
  _apt = (uint8_t)uiReadLong(server, "apt", _apt, 0, airportCount());

  String sla = server.arg("lat"), slo = server.arg("lon");
  float la = sla.toFloat(), lo = slo.toFloat();
  if (sla.length() > 0 && la >= -90 && la <= 90)   _lat = la;
  if (slo.length() > 0 && lo >= -180 && lo <= 180) _lon = lo;

  _radius   = (uint8_t)uiReadLong(server, "rad", _radius, 5, 100);
  _maxShown = (uint8_t)uiReadLong(server, "max", _maxShown, 1, AircraftList::CAP);
  _majorOnly  = server.hasArg("maj");
  _hideGround = server.hasArg("gnd");
  _showEmpty  = server.hasArg("emp");
  _showLogos = server.hasArg("lgo");
  _showStatus = server.hasArg("sta");
  _showNew = server.hasArg("new");
  _logoBright = (uint8_t)uiReadLong(server, "lgb", _logoBright, 10, 100);
  _units = (uint8_t)uiReadLong(server, "uni", _units, 0, 2);
  long p = uiReadLong(server, "pol", 1, 0, 2);
  _poll = (p == 0) ? 15 : (p == 2 ? 60 : 30);

  uint32_t c;
  if (uiReadColor(server, "cCall", c)) _colCall = c;
  if (uiReadColor(server, "cData", c)) _colData = c;
  if (uiReadColor(server, "cAcc", c))  _colAccent = c;

  store.putU8("apt", _apt);
  store.putFloat("lat", _lat);
  store.putFloat("lon", _lon);
  store.putU8("rad", _radius);
  store.putU8("max", _maxShown);
  store.putBool("maj", _majorOnly);
  store.putBool("gnd", _hideGround);
  store.putBool("emp", _showEmpty);
  store.putU8("uni", _units);
  store.putU8("pol", _poll);
  store.putBool("lgo", _showLogos);
  store.putBool("sta", _showStatus);
  store.putBool("new", _showNew);
  store.putU8("lgb", _logoBright);
  store.putUInt("cCall", _colCall);
  store.putUInt("cData", _colData);
  store.putUInt("cAcc", _colAccent);

  _fetchNow = true;             // look again with the new area
}

// Where the search is centered, and a short name for it.
void FlightModule::center(float &lat, float &lon, char *label, size_t n) {
  if (_apt > 0) {
    const Airport &a = airportAt(_apt - 1);
    lat = a.lat; lon = a.lon;
    snprintf(label, n, "%s", a.code);
  } else {
    lat = _lat; lon = _lon;
    label[0] = 0;
  }
}

String FlightModule::summary() {
  if (!_valid) {
    if (_lastStatus > 0 && _lastStatus != 200) return "Flight service answered with error " + String((int)_lastStatus);
    return _lastStatus < 0 ? "Could not read flight data yet" : "No flight data yet";
  }
  const AircraftList &l = _snap[_cur];
  String where = _snapArea[0] ? String(_snapArea) : String("your coordinates");
  String s = l.count() == 0 ? String("No aircraft") : String(l.count()) + (l.count() == 1 ? " aircraft" : " aircraft");
  s += " within " + String(_snapRadius) + " nm of " + where;
  if (millis() - _goodAt > STALE_MS) s += " (out of date)";
  return s;
}

String FlightModule::actionsHtml() {
  return "<form method='POST' action='/flights/refresh'><button class='sec' type='submit'>Update flights now</button></form>"
         "<form method='POST' action='/flights/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void FlightModule::registerRoutes(WebServer &server) {
  server.on("/flights/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/flights");
  });
  server.on("/flights/colors-reset", HTTP_POST, [this]() {
    _colCall = DEF_COL_CALL; _colData = DEF_COL_DATA; _colAccent = DEF_COL_ACCENT;
    store.putUInt("cCall", _colCall);
    store.putUInt("cData", _colData);
    store.putUInt("cAcc", _colAccent);
    app.requestRedraw();
    webRedirect("/flights?saved=1");
  });
}

// ---------- data (background task) ----------
void FlightModule::begin() {
  netWorkerAdd(this);                                       // downloads run in the shared network task
}

void FlightModule::netTick() {
  if (millis() < 22000) return;                            // start after Wi-Fi and other boot services are stable
  if (!enabled()) { _everFetched = false; return; }         // nothing is fetched while switched off

  unsigned long now = millis();
  unsigned long interval = _lastOk ? (unsigned long)_poll * 1000UL : RETRY_MS;
  if (_fetchNow || !_everFetched || now - _lastFetch >= interval) {
    _fetchNow = false;
    _lastOk = fetch();
    _lastFetch = millis();
    _everFetched = true;
    app.requestRedraw();
    Serial.println(_lastOk ? "Flights updated" : "Flights fetch failed");
  }
}

// Only one secure download runs at a time (the news ticker takes turns with this).
bool FlightModule::fetch() {
  if (!netLockTake(60000)) return false;
  bool ok = fetchInner();
  netLockGive();
  return ok;
}

bool FlightModule::fetchInner() {
  if (WiFi.status() != WL_CONNECTED) { _lastStatus = -1; return false; }

  // copy the settings this fetch uses (the web page may change them meanwhile)
  float lat, lon;
  char area[8];
  center(lat, lon, area, sizeof(area));
  const int  radius = _radius;
  const bool majorOnly = _majorOnly;
  const bool hide = _hideGround;
  const int  limit = _maxShown;
  const uint8_t units = _units;

  char url[112];
  snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d", lat, lon, radius);

  WiFiClientSecure client;
  client.setInsecure();                 // skips the certificate check; fine for a public data display
  HTTPClient http;
  http.setTimeout(10000);
  http.useHTTP10(true);                 // plain, un-chunked reply we can read as a stream
  http.setUserAgent("PixelPop-ESP32");
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  if (!http.begin(client, url)) { _lastStatus = -1; return false; }

  int status = http.GET();
  _lastStatus = status;
  if (status != 200) { http.end(); return false; }

  _work.setLimit(limit);
  _stream.begin(_work, lat, lon, (float)radius, hide, majorOnly);

  if (http.header("Transfer-Encoding").indexOf("chunked") >= 0) {
    // Unexpected, but handle it: let the library decode the chunks (needs memory).
    if (ESP.getFreeHeap() < 150000) { http.end(); _lastStatus = -1; return false; }
    String body = http.getString();
    for (size_t i = 0; i < body.length() && !_stream.done(); i++) _stream.feed(body[i]);
  } else {
    WiFiClient *s = http.getStreamPtr();
    uint8_t buf[512];
    unsigned long start = millis(), lastData = start;
    while (s && !_stream.done() && millis() - start < 25000) {
      int avail = s->available();
      if (avail > 0) {
        int n = s->read(buf, avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail);
        for (int i = 0; i < n; i++) _stream.feed((char)buf[i]);
        lastData = millis();
        vTaskDelay(pdMS_TO_TICKS(1));
      } else {
        if (!s->connected()) break;
        if (millis() - lastData > 8000) break;
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
  }
  http.end();

  if (!_stream.done()) { _lastStatus = -1; return false; }      // cut off or not what we expected

  // note which flights are new to the list (for the corner tag)
  char key[32];
  snprintf(key, sizeof(key), "%.3f,%.3f,%d", lat, lon, radius);
  stampSeen(_work, millis(), key);

  // publish: fill the copy the display is not reading, then switch
  uint8_t next = _cur ^ 1;
  _snap[next] = _work;
  snprintf(_snapArea, sizeof(_snapArea), "%s", area);
  _snapRadius = radius;
  _snapUnits = units;
  _cur = next;
  _goodAt = millis();
  _valid = true;
  Serial.printf("Flights: %d in reply, showing %d\n", _stream.seen(), _work.count());
  return true;
}

// A flight is "new" for NEW_MS after it first shows up in the list. Flights already in the
// list when tracking begins (start-up, or after changing the place) are never new. A flight
// that drops out and comes back within FORGET_MS keeps its original time.
static const unsigned long NEW_MS = 120000UL;        // 2 minutes
static const unsigned long FORGET_MS = 600000UL;     // 10 minutes

void FlightModule::stampSeen(AircraftList &l, unsigned long now, const char *key) {
  if (strcmp(key, _seenKey) != 0) {
    snprintf(_seenKey, sizeof(_seenKey), "%s", key);
    _nSeen = 0;
    _seenBaseline = false;
  }
  for (int i = 0; i < _nSeen;) {                      // forget flights not seen for a while
    if (now - _seen[i].last > FORGET_MS) _seen[i] = _seen[--_nSeen];
    else i++;
  }
  for (int i = 0; i < l.count(); i++) {
    Aircraft &a = l.at(i);
    int f = -1;
    for (int j = 0; j < _nSeen; j++) if (strcmp(_seen[j].call, a.call) == 0) { f = j; break; }
    if (f < 0) {
      if (_nSeen < SEEN_MAX) f = _nSeen++;
      else {                                          // table full: replace the one seen longest ago
        f = 0;
        for (int j = 1; j < _nSeen; j++) if (_seen[j].last < _seen[f].last) f = j;
      }
      snprintf(_seen[f].call, sizeof(_seen[f].call), "%s", a.call);
      _seen[f].first = _seenBaseline ? (now ? now : 1) : 0;
    }
    _seen[f].last = now;
    a.firstSeen = (uint32_t)_seen[f].first;
  }
  _seenBaseline = true;
}

bool FlightModule::isFresh(const Aircraft &a) const {
  return _showNew && a.firstSeen != 0 && millis() - a.firstSeen < NEW_MS;
}

// ---------- drawing ----------
int FlightModule::pageCount() {
  int n = _valid ? _snap[_cur].count() : 0;
  return n > 0 ? n : 1;
}

bool FlightModule::pageAvailable(int sub) {
  if (!_valid) return false;
  if (millis() - _goodAt > STALE_MS) return false;         // data too old to be worth showing
  int n = _snap[_cur].count();
  if (n == 0) return _showEmpty && sub == 0;
  return sub < n;
}

void FlightModule::drawPage(int sub) {
  unsigned long now = millis();
  if (now - _lastDraw > 500 || sub != _lastSub) _scrollT0 = now;     // a fresh showing: scroll from the start
  _lastDraw = now;
  _lastStep = (now - _scrollT0) / SCROLL_STEP;
  _lastSub = sub;
  _scrolling = false;
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  const AircraftList &l = _snap[_cur];
  if (l.count() == 0)        drawEmpty();
  else if (sub < l.count())  drawAircraft(l.at(sub), sub, l.count());
  gDisplay->flipDMABuffer();
}


// Small text (3x5 pixel font, 4 px per character). The cursor y is the baseline.
static int smallW(const char *s) { return (int)strlen(s) * 4 - 1; }
static void printSmall(int x, int baseline, const char *s, uint16_t col) {
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(s);
  gDisplay->setFont(nullptr);
}
static const int ALT_BASE = 22;                // baseline of the altitude row (glyphs fill rows 17-21)
static const int SMALL_BASE = 29;              // baseline of the bottom row (glyphs fill rows 24-28)

// Flight status (CLIMB, DESC, CRUISE, ... or an emergency) in the space between xs and xe on the
// altitude row. If it is too wide it pauses, scrolls left through the space, and repeats.
void FlightModule::drawStatus(const Aircraft &a, int xs, int xe) {
  if (!_showStatus || xe - xs < 12) return;
  int kind = 0;
  const char *st = flightStatus(a, &kind);
  if (!st) return;
  uint32_t c = _colAccent;
  if (kind == 1) c = 0x30E060; else if (kind == 2) c = 0xFF7020; else if (kind == 4) c = 0xFF3030;
  const uint16_t col = rgb565(c);
  const int tw = smallW(st), avail = xe - xs;
  if (tw <= avail) { printSmall(xs, ALT_BASE, st, col); return; }

  _scrolling = true;
  const int period = tw + 8;                        // text plus a gap before it comes round again
  const unsigned long PAUSE = 1500, STEP = SCROLL_STEP;      // hold still first, then one pixel per 70 ms
  unsigned long t = (millis() - _scrollT0) % (PAUSE + (unsigned long)period * STEP);
  int off = (t < PAUSE) ? 0 : (int)((t - PAUSE) / STEP);
  gDisplay->setClipX(xs, xe);
  printSmall(xs - off, ALT_BASE, st, col);
  printSmall(xs - off + period, ALT_BASE, st, col);
  gDisplay->clearClip();
}

bool FlightModule::needsRedraw() {
  const unsigned long typePhase = millis() / 3000;
  if (typePhase != _lastTypePhase) {
    _lastTypePhase = typePhase;
    return true;
  }
  return _scrolling && (millis() - _scrollT0) / SCROLL_STEP != _lastStep;     // the text moved a pixel
}

// Bottom row: speed and distance as dark text on a light bar across the full width.
// The bar uses the "data" color; the speed is black and the distance is dark purple.
void FlightModule::drawBottomBar(const char *sp, const char *di) {
  gDisplay->fillRect(0, SMALL_BASE - 6, gDisplay->width(), 7, rgb565(_colData));   // rows 23-29
  printSmall(1, SMALL_BASE, sp, rgb565(0x000000));
  printSmall(1 + smallW(sp) + 5, SMALL_BASE, di, rgb565(0x6A1B9A));      // dark purple
}

// Red "dog ear" in the lower-right corner: a small folded-corner triangle.
static void drawNewTag() {
  const int S = 7;
  const int right = gDisplay->width() - 1;
  const int bottom = gDisplay->height() - 1;
  const uint16_t red = rgb565(0xE00000), fold = rgb565(0xFF7070);
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S - y; x++)
      gDisplay->drawPixel(right - x, bottom - y, (x + y == S - 1) ? fold : red);
}

// Bold text with an extra pixel of gap between characters (drawn one character at a time,
// since the built-in font's automatic advance can't otherwise be widened per string).
static int boldSpacedWidth(const char *s) {
  const int n = (int)strlen(s);
  return n > 0 ? n * 7 - 1 : 0;                     // 6px bold glyph + 1px gap, minus the last glyph's trailing gap
}
static void printBoldSpaced(int x, int y, const char *s, uint16_t col) {
  gDisplay->setTextColor(col);
  for (const char *p = s; *p; p++) {
    gDisplay->setCursor(x, y);
    gDisplay->print(*p);
    gDisplay->setCursor(x + 1, y);                  // the bold "double draw" offset
    gDisplay->print(*p);
    x += 7;                                         // 6px normal advance + 1px extra letter-spacing
  }
}

// The flight number/callsign, bold with a pixel of air between characters. If that doesn't fit
// between x and xe, it pauses, scrolls left through the space, and repeats (same pattern as
// drawStatus() and drawTypeRow() below).
void FlightModule::drawCallsign(int x, int y, int xe, const char *s, uint16_t col) {
  const int w = boldSpacedWidth(s);
  if (w <= xe - x) { printBoldSpaced(x, y, s, col); return; }

  _scrolling = true;
  const int period = w + 8;
  const unsigned long PAUSE = 1500, STEP = SCROLL_STEP;
  unsigned long t = (millis() - _scrollT0) % (PAUSE + (unsigned long)period * STEP);
  int off = (t < PAUSE) ? 0 : (int)((t - PAUSE) / STEP);
  gDisplay->setClipX(x, xe);
  printBoldSpaced(x - off, y, s, col);
  printBoldSpaced(x - off + period, y, s, col);
  gDisplay->clearClip();
}

// Arrow pointing in the direction of travel (0 = up/north), centered at (cx, cy).
static void drawHeadingArrow(int cx, int cy, int r, int deg, uint16_t col) {
  const float a = deg * 0.017453292f;
  auto px = [&](float ang, float rad) { return cx + (int)lroundf(sinf(ang) * rad); };
  auto py = [&](float ang, float rad) { return cy - (int)lroundf(cosf(ang) * rad); };
  gDisplay->fillTriangle(px(a, r), py(a, r),
                         px(a + 2.5f, r * 0.85f), py(a + 2.5f, r * 0.85f),
                         px(a - 2.5f, r * 0.85f), py(a - 2.5f, r * 0.85f), col);
}

static const char *aircraftModelName(const char *type) {
  struct Model { const char *code; const char *name; };
  static const Model MODELS[] = {
    {"A319", "Airbus A319"}, {"A320", "Airbus A320"}, {"A321", "Airbus A321"},
    {"A20N", "Airbus A320neo"}, {"A21N", "Airbus A321neo"},
    {"A332", "Airbus A330-200"}, {"A333", "Airbus A330-300"},
    {"A359", "Airbus A350-900"}, {"A35K", "Airbus A350-1000"},
    {"B38M", "Boeing 737 MAX 8"}, {"B39M", "Boeing 737 MAX 9"},
    {"B732", "Boeing 737-200"}, {"B733", "Boeing 737-300"},
    {"B734", "Boeing 737-400"}, {"B735", "Boeing 737-500"},
    {"B736", "Boeing 737-600"}, {"B737", "Boeing 737-700"},
    {"B738", "Boeing 737-800"}, {"B739", "Boeing 737-900"},
    {"B741", "Boeing 747-100"}, {"B744", "Boeing 747-400"},
    {"B752", "Boeing 757-200"}, {"B753", "Boeing 757-300"},
    {"B762", "Boeing 767-200"}, {"B763", "Boeing 767-300"},
    {"B772", "Boeing 777-200"}, {"B77W", "Boeing 777-300ER"},
    {"B788", "Boeing 787-8"}, {"B789", "Boeing 787-9"}, {"B78X", "Boeing 787-10"},
    {"C172", "Cessna 172"}, {"CRJ2", "Bombardier CRJ200"},
    {"CRJ7", "Bombardier CRJ700"}, {"CRJ9", "Bombardier CRJ900"},
    {"E170", "Embraer 170"}, {"E75L", "Embraer 175"}, {"E190", "Embraer 190"}
  };
  for (const Model &model : MODELS)
    if (strcmp(type, model.code) == 0) return model.name;
  return nullptr;
}

static const char *aircraftTypeText(const Aircraft &a) {
  if (!a.type[0]) return a.reg;
  const char *model = aircraftModelName(a.type);
  return model && ((millis() / 3000) & 1) ? model : a.type;
}

void FlightModule::drawTypeRow(int x, int y, int right, const char *text) {
  if (!text[0]) return;
  const int width = (int)strlen(text) * 6 - 1;
  gDisplay->setTextColor(rgb565(_colData));
  if (width <= right - x) {
    gDisplay->setCursor(x, y);
    gDisplay->print(text);
    return;
  }
  _scrolling = true;
  const int period = width + 8;
  const int offset = (int)(((millis() - _scrollT0) / SCROLL_STEP) % (unsigned long)period);
  gDisplay->setClipX(x, right);
  gDisplay->setCursor(right - offset, y);
  gDisplay->print(text);
  gDisplay->setCursor(right - offset + period, y);
  gDisplay->print(text);
  gDisplay->clearClip();
}

// Portrait keeps every aircraft fact in an independent row. Long text scrolls
// within that row instead of colliding with another field.
void FlightModule::drawPortrait(const Aircraft &a, const char *logoCode) {
  const int W = gDisplay->width();
  const int units = _snapUnits;
  const uint16_t call = rgb565(_colCall), data = rgb565(_colData), accent = rgb565(_colAccent);
  auto row = [this, W](int baseline, const char *text, uint16_t color) {
    const int width = smallW(text);
    if (width <= W - 2) {
      printSmall((W - width) / 2, baseline, text, color);
      return;
    }
    _scrolling = true;
    const int period = width + 8;
    const int offset = (int)(((millis() - _scrollT0) / SCROLL_STEP) % (unsigned long)period);
    gDisplay->setClipX(0, W);
    printSmall(W - offset, baseline, text, color);
    printSmall(W - offset + period, baseline, text, color);
    gDisplay->clearClip();
  };

  const int logoInset = (W - logoSize()) / 2;
  const int firstRow = logoCode ? 33 : 23;
  if (logoCode) logoDraw(logoCode, logoInset, logoInset, _logoBright);
  else row(7, aircraftTypeText(a), data);

  row(firstRow, a.call, call);
  char altitude[24], speed[16], distance[16];
  fmtAltitude(altitude, sizeof(altitude), a, units);
  uint32_t altitudeColor = _colData;
  if (!a.onGround && a.vrate >= 300) altitudeColor = 0x30E060;
  else if (!a.onGround && a.vrate <= -300) altitudeColor = 0xFF7020;
  row(firstRow + 8, altitude, rgb565(altitudeColor));
  fmtSpeed(speed, sizeof(speed), a, units);
  row(firstRow + 16, speed, data);
  fmtDistance(distance, sizeof(distance), a, units, true);
  row(firstRow + 24, distance, accent);
}

void FlightModule::drawAircraft(const Aircraft &a, int idx, int total) {
  char code[4];
  const bool haveLogo = _showLogos && logoCodeFromCallsign(a.call, code) && logoAvailable(code);
  if (gDisplay->height() > gDisplay->width()) {
    drawPortrait(a, haveLogo ? code : nullptr);
    return;
  }
  if (haveLogo) {
    drawWithLogo(a, code, idx, total);
    return;
  }
  const int W = gDisplay->width();
  const int units = _snapUnits;
  char buf[24];

  // row 1: callsign in bold, a pixel of air between characters, scrolling if it doesn't fit
  drawCallsign(isFresh(a) ? 8 : 1, 0, W - 1, a.call, rgb565(_colCall));   // shifted right of the new-flight tag

  // row 2: alternate the ICAO aircraft code with its resolved model name.
  drawTypeRow(1, 8, W - 9, aircraftTypeText(a));

  // row 3: altitude - green while climbing, orange while descending
  fmtAltitude(buf, sizeof(buf), a, units);
  uint32_t altCol = _colData;
  if (!a.onGround && a.vrate >= 300)       altCol = 0x30E060;
  else if (!a.onGround && a.vrate <= -300) altCol = 0xFF7020;
  printSmall(1, ALT_BASE, buf, rgb565(altCol));
  drawStatus(a, 1 + smallW(buf) + 4, W - 15);

  // heading arrow aligned with the aircraft type/model row
  if (a.track >= 0 && !a.onGround) {
    drawHeadingArrow(W - 5, 11, 3, a.track, rgb565(_colAccent));
  }

  // row 4: speed, then distance from the center (small font)
  char sp[12], di[12];
  fmtSpeed(sp, sizeof(sp), a, units);
  fmtDistance(di, sizeof(di), a, units, true);
  if (smallW(sp) + 5 + smallW(di) > W - 2) fmtDistance(di, sizeof(di), a, units, false);
  drawBottomBar(sp, di);
  if (isFresh(a)) drawNewTag();
}

void FlightModule::drawEmpty() {
  char buf[24];
  gDisplay->setTextColor(rgb565(_colCall));
  gDisplay->setCursor(2, 3);
  gDisplay->print("No flights");
  gDisplay->setTextColor(rgb565(_colData));
  snprintf(buf, sizeof(buf), "in %dnm", _snapRadius);
  gDisplay->setCursor(2, 13);
  gDisplay->print(buf);
  gDisplay->setTextColor(rgb565(_colAccent));
  snprintf(buf, sizeof(buf), "of %s", _snapArea[0] ? _snapArea : "center");
  gDisplay->setCursor(2, 23);
  gDisplay->print(buf);
}

// ---- layout with the airline logo: a small logo at the top left, the data around and below it ----
void FlightModule::drawWithLogo(const Aircraft &a, const char *code, int idx, int total) {
  logoDraw(code, 0, 0, _logoBright);

  const int W = gDisplay->width();
  const int units = _snapUnits;
  const int x0 = logoSize() + 2;                     // text column beside the logo
  char buf[24];

  // beside the logo: callsign in bold, a pixel of air between characters, then the aircraft type/model
  drawCallsign(x0, 0, W - 1, a.call, rgb565(_colCall));

  drawTypeRow(x0, 8, W - 8, aircraftTypeText(a));

  // below the logo: altitude (green while climbing, orange while descending)
  fmtAltitude(buf, sizeof(buf), a, units);
  uint32_t altCol = _colData;
  if (!a.onGround && a.vrate >= 300)       altCol = 0x30E060;
  else if (!a.onGround && a.vrate <= -300) altCol = 0xFF7020;
  printSmall(1, ALT_BASE, buf, rgb565(altCol));
  drawStatus(a, 1 + smallW(buf) + 4, W - 11);
  if (a.track >= 0 && !a.onGround) drawHeadingArrow(W - 5, 11, 3, a.track, rgb565(_colAccent));

  // last row: speed, then distance from the center (small font)
  char sp[12], di[12];
  fmtSpeed(sp, sizeof(sp), a, units);
  fmtDistance(di, sizeof(di), a, units, true);
  if (smallW(sp) + 5 + smallW(di) > W - 2) fmtDistance(di, sizeof(di), a, units, false);
  drawBottomBar(sp, di);
  if (isFresh(a)) drawNewTag();
}
