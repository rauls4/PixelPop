#include "Forecast.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdlib.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../../core/TimeService.h"
#include "../../core/WxIcons.h"
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSans9pt7b.h>

static const unsigned long REFRESH_MS = 30UL * 60 * 1000;   // fetch every 30 minutes
static const unsigned long RETRY_MS   = 30UL * 1000;        // retry after a failure (once the quick tries are used up)
// After a failed try, try again quickly a few times (3, 4, 6, 10, 15 seconds), then every RETRY_MS.
static const unsigned long QUICK_RETRY_MS[5] = {3000, 4000, 6000, 10000, 15000};

static const uint32_t DEF_COL_HIGH = 0xFF9040;
static const uint32_t DEF_COL_LOW  = 0x40A0FF;
static const uint32_t WEATHER_CONDITION_COLOR = 0xFFD200;

static const char *const DAY_SHORT[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const DAY_LONG[7]  = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday"};

// ---------- settings ----------
void ForecastModule::onLoad() {
  _same     = store.getBool("same", true);
  _celsius  = store.getBool("cel", false);
  _lat      = store.getFloat("lat", 41.8745f);
  _lon      = store.getFloat("lon", -87.6512f);
  _layout   = store.getU8("lay", 0);
  if (_layout > 1) _layout = 0;
  _days     = store.getU8("days", 4);
  if (_days < 1) _days = 1;
  if (_days > MAX_DAYS) _days = MAX_DAYS;
  _colHigh  = store.getUInt("cHi", DEF_COL_HIGH);
  _colLow   = store.getUInt("cLo", DEF_COL_LOW);
}

String ForecastModule::onSettingsHtml() {
  String h;
  h += uiSection("Look");
  static const char *const looks[] = {"Two days per screen (icon, high / low)",
                                      "One day per screen (big high, low, rain chance)"};
  h += uiSelect("lay", "Layout", looks, 2, _layout);
  h += uiNumber("days", "Days to show (1 to 7)", _days, 1, MAX_DAYS);

  h += uiSection("Location and units");
  h += uiCheckbox("same", "Same location and units as the Weather page", _same);
  static const char *const units[] = {"\xC2\xB0""F", "\xC2\xB0""C"};
  h += uiSelect("units", "Temperature (when not using the Weather page's)", units, 2, _celsius ? 1 : 0);
  h += "<label>Latitude</label><input type='number' name='lat' step='any' min='-90' max='90' value='" +
       String(_lat, 4) + "'>";
  h += "<label>Longitude</label><input type='number' name='lon' step='any' min='-180' max='180' value='" +
       String(_lon, 4) + "'>";
  h += "<p class='m'>The latitude, longitude and units above are only used when the box above is unchecked.</p>";

  h += uiSection("Colors");
  h += uiColor("cHi", "High temperature", _colHigh);
  h += uiColor("cLo", "Low temperature", _colLow);
  return h;
}

void ForecastModule::onSave(WebServer &server) {
  _same    = server.hasArg("same");
  _celsius = (server.arg("units") == "1");
  _layout  = (uint8_t)uiReadLong(server, "lay", _layout, 0, 1);
  _days    = (uint8_t)uiReadLong(server, "days", _days, 1, MAX_DAYS);

  String sla = server.arg("lat"), slo = server.arg("lon");
  float la = sla.toFloat(), lo = slo.toFloat();
  if (sla.length() > 0 && la >= -90 && la <= 90)   _lat = la;
  if (slo.length() > 0 && lo >= -180 && lo <= 180) _lon = lo;

  uint32_t c;
  if (uiReadColor(server, "cHi", c))  _colHigh = c;
  if (uiReadColor(server, "cLo", c))  _colLow = c;

  store.putBool("same", _same);
  store.putBool("cel", _celsius);
  store.putU8("lay", _layout);
  store.putU8("days", _days);
  store.putFloat("lat", _lat);
  store.putFloat("lon", _lon);
  store.putUInt("cHi", _colHigh);
  store.putUInt("cLo", _colLow);

  _fetchNow = true;
}

String ForecastModule::summary() {
  if (!_valid) {
    if (_status[0]) return String("No forecast data yet - ") + _status + " (trying again)";
    return "No forecast data yet";
  }
  String s = String((int)roundf(_hi[0])) + "/" + String((int)roundf(_lo[0])) + "\xC2\xB0" + tUnit() +
             " " + DAY_LONG[_wday[0]] + ", " + wxConditionText(_code[0]);
  s += ", " + String(shownDays()) + (shownDays() == 1 ? " day" : " days");
  return s;
}

String ForecastModule::actionsHtml() {
  return "<form method='POST' action='/forecast/refresh'><button class='sec' type='submit'>Refresh forecast now</button></form>"
         "<form method='POST' action='/forecast/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void ForecastModule::registerRoutes(WebServer &server) {
  server.on("/forecast/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/forecast");
  });
  server.on("/forecast/colors-reset", HTTP_POST, [this]() {
    _colHigh = DEF_COL_HIGH; _colLow = DEF_COL_LOW;
    store.putUInt("cHi", _colHigh);
    store.putUInt("cLo", _colLow);
    app.requestRedraw();
    webRedirect("/forecast?saved=1");
  });
}

// ---------- data ----------
// Location and units in effect: the Weather module's saved ones, or our own.
void ForecastModule::resolve(float &lat, float &lon, bool &cel) {
  if (_same) {
    Store w("w_");
    lat = w.getFloat("lat", 41.8745f);
    lon = w.getFloat("lon", -87.6512f);
    cel = w.getBool("cel", false);
  } else {
    lat = _lat; lon = _lon; cel = _celsius;
  }
}

// Read up to `max` numbers from  "key":[ 1, 2, null, ... ]  found after `from`.
static int parseNumArray(const String &body, int from, const char *key, float *out, int max) {
  String k = String("\"") + key + "\":[";
  int i = body.indexOf(k, from);
  if (i < 0) return 0;
  const char *base = body.c_str();
  const char *p = base + i + k.length();
  int n = 0;
  while (n < max && *p && *p != ']') {
    char *end = nullptr;
    float v = strtof(p, &end);
    if (end == p) {                   // "null" or anything else: unknown value
      v = NAN;
      while (*p && *p != ',' && *p != ']') p++;
    } else {
      p = end;
    }
    out[n++] = v;
    if (*p == ',') p++;
  }
  return n;
}

// Read up to `max` "YYYY-MM-DD" strings from  "time":[ ... ]  found after `from`.
static int parseDates(const String &body, int from, int *wday, int max) {
  int i = body.indexOf("\"time\":[", from);
  if (i < 0) return 0;
  const char *p = body.c_str() + i + 8;
  int n = 0;
  while (n < max && *p && *p != ']') {
    if (*p == '"') {
      int y = 0, m = 0, d = 0;
      if (sscanf(p + 1, "%d-%d-%d", &y, &m, &d) == 3 && y > 2000) {
        long days = daysFromCivil(y, m, d);            // 1970-01-01 was a Thursday
        wday[n++] = (int)(((days % 7) + 4 + 7) % 7);   // 0 = Sunday
      }
      p = strchr(p + 1, '"');                          // closing quote
      if (!p) break;
    }
    p++;
  }
  return n;
}

void ForecastModule::setStatus(const char *s) {
  strncpy(_status, s, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = 0;
}

bool ForecastModule::fetch() {
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return false; }

  float lat, lon; bool cel;
  resolve(lat, lon, cel);

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(lat, 4) +
               "&longitude=" + String(lon, 4) +
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
               "&forecast_days=7"
               "&temperature_unit=" + (cel ? "celsius" : "fahrenheit") +
               "&timezone=auto";

  WiFiClientSecure client;
  client.setInsecure();   // skips certificate check; fine for a public weather display
  client.setTimeout(8);   // seconds (connecting)
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  if (!http.begin(client, url)) { setStatus("could not start the request"); return false; }
  int status = http.GET();
  if (status != 200) {
    char tls[40] = "";
    client.lastError(tls, sizeof(tls));                       // the encryption library's own reason, if any
    http.end();
    if (status > 0) { char m[48]; snprintf(m, sizeof(m), "the server answered %d", status); setStatus(m); }
    else netExplain(_status, sizeof(_status), status, "api.open-meteo.com", tls);
    Serial.print("Forecast: ");
    Serial.println(_status);
    return false;
  }
  String body = http.getString();
  http.end();

  int daily = body.indexOf("\"daily\":{");       // not "daily_units"
  if (daily < 0) { setStatus("the reply was not understood"); return false; }

  int   wday[MAX_DAYS];
  float code[MAX_DAYS], hi[MAX_DAYS], lo[MAX_DAYS], pop[MAX_DAYS];
  int nd = parseDates(body, daily, wday, MAX_DAYS);
  int nc = parseNumArray(body, daily, "weather_code", code, MAX_DAYS);
  int nh = parseNumArray(body, daily, "temperature_2m_max", hi, MAX_DAYS);
  int nl = parseNumArray(body, daily, "temperature_2m_min", lo, MAX_DAYS);
  int np = parseNumArray(body, daily, "precipitation_probability_max", pop, MAX_DAYS);

  int n = nd;
  if (nc < n) n = nc;
  if (nh < n) n = nh;
  if (nl < n) n = nl;
  if (n < 1) { setStatus("the reply was not understood"); return false; }

  int kept = 0;
  // Weather already owns today's conditions; Forecast begins with tomorrow.
  for (int i = 1; i < n; i++) {
    if (isnan(hi[i]) || isnan(lo[i]) || isnan(code[i])) continue;
    _wday[kept] = wday[i];
    _code[kept] = (int)code[i];
    _hi[kept] = hi[i];
    _lo[kept] = lo[i];
    _pop[kept] = (i < np && !isnan(pop[i])) ? (int)roundf(pop[i]) : -1;
    kept++;
  }
  if (kept < 1) { setStatus("the reply had no future days"); return false; }

  _nData = kept;
  _dataCel = cel;             // the units the numbers are in (shown as the unit letter)
  _uLat = lat; _uLon = lon; _uCel = cel;
  _valid = true;
  _status[0] = 0;
  return true;
}

void ForecastModule::begin() {
  _everFetched = false;
  netWorkerAdd(this);                                        // downloads run in the shared network task
}

void ForecastModule::netTick() {
  if (millis() < 2500) return;                               // let Wi-Fi settle after start-up
  if (!enabled()) return;                                    // only fetch while the function is switched on
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return; }   // not a failed try
  const unsigned long now = millis();

  // If the Weather page's location or units changed, fetch again.
  if (_same && _valid && now - _lastCheck > 5000) {
    _lastCheck = now;
    float la, lo; bool ce;
    resolve(la, lo, ce);
    if (fabsf(la - _uLat) > 0.00005f || fabsf(lo - _uLon) > 0.00005f || ce != _uCel) _fetchNow = true;
  }

  const unsigned long interval = _lastOk ? REFRESH_MS : (_fails < 5 ? QUICK_RETRY_MS[_fails] : RETRY_MS);
  if (_fetchNow || !_everFetched || now - _lastFetch >= interval) {
    _fetchNow = false;
    bool ok = false;
    if (netLockTake(60000)) {                                // one secure download at a time
      ok = fetch();
      netLockGive();
    } else setStatus("the network was busy");
    _lastOk = ok;
    if (ok) _fails = 0; else if (_fails < 250) _fails++;
    _lastFetch = millis();
    _everFetched = true;
    app.requestRedraw();
    Serial.println(ok ? "Forecast updated" : "Forecast fetch failed");
  }
}

// ---------- drawing ----------
int ForecastModule::pageCount() {
  int n = shownDays();
  if (n < 1) n = 1;
  if (gDisplay && gDisplay->height() > gDisplay->width()) return n;
  return _layout == 0 ? (n + 1) / 2 : n;
}

void ForecastModule::drawPage(int sub) {
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  if (gDisplay->height() > gDisplay->width()) drawPortrait(sub);
  else if (_layout == 0) drawTwo(sub);
  else                   drawOne(sub);
  gDisplay->flipDMABuffer();
}

bool ForecastModule::needsRedraw() {
  if (!gDisplay || gDisplay->height() <= gDisplay->width()) return false;
  const unsigned long step = (millis() - _scrollT0) / 70;
  if (step == _lastScrollStep) return false;
  _lastScrollStep = step;
  return true;
}

int ForecastModule::transitionOverride() {
  return gDisplay && gDisplay->height() > gDisplay->width() ? 3 : -1;  // slide left
}

// Text width of a string at size 1
static int tw(const char *s) { return (int)strlen(s) * 6 - 1; }
static int tinyTw(const char *s) { return (int)strlen(s) * 4 - 1; }

// Two days per screen: day name, icon, "hi/lo".
void ForecastModule::drawTwo(int page) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const int n = shownDays();
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextSize(1);
  for (int c = 0; c < 2; c++) {
    int d = page * 2 + c;
    if (d >= n) break;
    const bool alone = (page * 2 + 1 >= n);           // last page with a single day: center it
    const int colW = W / 2;
    const int cx = alone ? W / 2 : c * colW + colW / 2;

    // day name
    const char *name = DAY_SHORT[_wday[d]];
    gDisplay->setTextColor(rgb565(WEATHER_CONDITION_COLOR));
    gDisplay->setCursor(cx - tinyTw(name) / 2, 6);
    gDisplay->print(name);

    // Large shaded icon is centered between the title and its reserved temperature row.
    drawWxIcon(cx, 15, _code[d], true, true);

    // High/low values use a dedicated tiny-font baseline below the icon.
    char a[8], b[8];
    snprintf(a, sizeof(a), "%d", (int)roundf(_hi[d]));
    snprintf(b, sizeof(b), "%d", (int)roundf(_lo[d]));
    const int total = tinyTw(a) + 1 + tinyTw(b) + 4;  // slash plus degree ring
    int x = cx - total / 2;
    const int y = 29;
    gDisplay->setTextColor(rgb565(_colHigh));
    gDisplay->setCursor(x, y);
    gDisplay->print(a);
    x += (int)strlen(a) * 4;
    gDisplay->setTextColor(COL_GRAY);
    gDisplay->setCursor(x, y);
    gDisplay->print("/");
    x += 3;
    gDisplay->setTextColor(rgb565(_colLow));
    gDisplay->setCursor(x, y);
    gDisplay->print(b);
    drawWxDegree(x + tinyTw(b) + 1, y - 5, rgb565(_colLow));
  }
  // Full-height divider keeps the two daily cards visually distinct.
  if (page * 2 + 1 < n) {
    gDisplay->drawFastVLine(W / 2, 0, H, COL_GRAY);
  }
  gDisplay->setFont(nullptr);
}

// Portrait: a daily card matching Weather's temperature / animated-icon / footer structure.
void ForecastModule::drawPortrait(int d) {
  if (d >= shownDays()) return;
  const int W = gDisplay->width(), H = gDisplay->height();
  const char *name = DAY_SHORT[_wday[d]];
  const unsigned long now = millis();

  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(rgb565(WEATHER_CONDITION_COLOR));
  gDisplay->setCursor((W - ((int)strlen(name) * 4 - 1)) / 2, 6);
  gDisplay->print(name);

  char high[8];
  snprintf(high, sizeof(high), "%d", (int)roundf(_hi[d]));
  gDisplay->setFont(&FreeSans9pt7b);
  gDisplay->setTextColor(COL_WHITE);
  int16_t highX, highY;
  uint16_t highW, highH;
  gDisplay->getTextBounds(high, 0, 0, &highX, &highY, &highW, &highH);
  const int highCursorX = (W - (int)highW - 4) / 2 - highX;  // 1 px gap plus 3 px degree ring
  const int highBaseline = 23;
  gDisplay->setCursor(highCursorX, highBaseline);
  gDisplay->print(high);
  drawWxDegree(highCursorX + highX + highW + 1, highBaseline - 12, COL_WHITE);
  drawWxIcon(W / 2, 38, _code[d], true, true);

  char details[72];
  const char *condition = wxConditionText(_code[d]);
  if (_pop[d] >= 0) {
    snprintf(details, sizeof(details), "%s  HIGH %d  LOW %d  RAIN %d%%",
             condition, (int)roundf(_hi[d]), (int)roundf(_lo[d]), _pop[d]);
  } else {
    snprintf(details, sizeof(details), "%s  HIGH %d  LOW %d",
             condition, (int)roundf(_hi[d]), (int)roundf(_lo[d]));
  }
  const int detailW = (int)strlen(details) * 4 - 1;
  const int period = detailW + 8;
  const int off = (int)(((now - _scrollT0) / 70) % (unsigned long)period);
  gDisplay->setClipX(0, W);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(COL_WHITE);
  gDisplay->setCursor(W - off, 61);
  gDisplay->print(details);
  gDisplay->setCursor(W - off + period, 61);
  gDisplay->print(details);
  gDisplay->clearClip();
  const int n = shownDays();
  for (int i = 0; i < n; i++) {
    const int x = (W - n * 3 + 1) / 2 + i * 3;
    gDisplay->fillRect(x, 53, 2, 1, i == d ? COL_WHITE : COL_GRAY);
  }
  gDisplay->setFont(nullptr);
}

// One day per screen: name on top, big high, low + rain chance below, icon at right.
void ForecastModule::drawOne(int d) {
  if (d >= shownDays()) return;
  const int W = gDisplay->width();

  const char *name = DAY_LONG[_wday[d]];
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(rgb565(WEATHER_CONDITION_COLOR));
  gDisplay->setCursor((W - tinyTw(name)) / 2, 6);
  gDisplay->print(name);
  gDisplay->setFont(nullptr);

  drawWxIcon(W - 11, 17, _code[d], true, true);

  char buf[12];
  snprintf(buf, sizeof(buf), "%d", (int)roundf(_hi[d]));
  gDisplay->setTextSize(2);
  gDisplay->setTextColor(rgb565(_colHigh));
  gDisplay->setCursor(1, 8);
  gDisplay->print(buf);
  drawWxDegree(2 + (int)strlen(buf) * 12, 9, rgb565(_colHigh));
  // low, then the rain chance if there is room before the icon
  snprintf(buf, sizeof(buf), "L%d", (int)roundf(_lo[d]));
  gDisplay->setTextColor(rgb565(_colLow));
  gDisplay->setCursor(1, 23);
  gDisplay->print(buf);
  drawWxDegree(2 + (int)strlen(buf) * 12, 24, rgb565(_colLow));
  int x = 1 + (int)strlen(buf) * 6 + 5;
  if (_pop[d] >= 0) {
    char p[8];
    snprintf(p, sizeof(p), "%d%%", _pop[d]);
    if (x + tw(p) + 4 <= W - 22) {
      // little rain drop
      gDisplay->drawPixel(x + 1, 23, COL_BLUE);
      gDisplay->drawFastHLine(x, 24, 3, COL_BLUE);
      gDisplay->drawFastHLine(x, 25, 3, COL_BLUE);
      gDisplay->drawPixel(x + 1, 26, COL_BLUE);
      gDisplay->setTextColor(COL_WHITE);
      gDisplay->setCursor(x + 5, 23);
      gDisplay->print(p);
    }

  }
}
