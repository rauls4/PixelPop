#include "Weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include <Fonts/TomThumb.h>
#include "../../core/SmoothFont.h"
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WxIcons.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"

static const unsigned long REFRESH_MS = 10UL * 60 * 1000;   // fetch every 10 minutes
static const unsigned long RETRY_MS   = 30UL * 1000;        // slow retry, once the quick ones have failed
// The first tries after a failure come quickly (a network that has only just come up often fails once),
// then they settle down to RETRY_MS.
static const unsigned long QUICK_RETRY_MS[5] = {3000, 4000, 6000, 10000, 15000};

static const uint32_t DEF_COL_TEMP   = 0xFFFFFF;   // big temperature
static const uint32_t DEF_COL_COND   = 0xFFD200;   // condition text
static const uint32_t DEF_COL_DETAIL = 0xFFFFFF;   // the scrolling details
static const uint32_t DEF_COL_ACCENT = 0x00C8FF;   // unit letter

// ---------- settings ----------
static const int CITY_MAX = 20;
// Plain ASCII only (the panel font has nothing else), trimmed, and short enough to scroll comfortably.
static String cleanCity(const String &in) {
  String out;
  for (unsigned i = 0; i < in.length() && (int)out.length() < CITY_MAX; i++) {
    char ch = in[i];
    if (ch >= 32 && ch <= 126) out += ch;
  }
  out.trim();
  return out;
}

void WeatherModule::onLoad() {
  _celsius     = store.getBool("cel", false);
  _lat         = store.getFloat("lat", 41.8745f);
  _lon         = store.getFloat("lon", -87.6512f);
  _city        = cleanCity(store.getString("city", "Chicago"));
  _colTemp     = store.getUInt("cTemp", DEF_COL_TEMP);
  _colCond     = store.getUInt("cCond", DEF_COL_COND);
  _colDetail   = store.getUInt("cDet", DEF_COL_DETAIL);
  _colAccent   = store.getUInt("cAcc", DEF_COL_ACCENT);
}

String WeatherModule::onSettingsHtml() {
  String h;
  h += uiSection("Units");
  static const char *const units[] = {"\xC2\xB0""F, mph", "\xC2\xB0""C, km/h"};
  h += uiSelect("units", "Temperature and wind", units, 2, _celsius ? 1 : 0);

  h += uiSection("Location");
  h += uiText("city", "City name (scrolls before the weather)", _city, CITY_MAX);
  h += "<label>Latitude</label><input type='number' name='lat' step='any' min='-90' max='90' value='" +
       String(_lat, 4) + "'>";
  h += "<label>Longitude</label><input type='number' name='lon' step='any' min='-180' max='180' value='" +
       String(_lon, 4) + "'>";
  h += "<p class='m'>Weather is looked up by coordinates. Default is Chicago. The city name is only a label: it does not move the location, so change the coordinates too. Find your coordinates by searching your address on any map site.</p>";

  h += uiSection("Colors");
  h += uiColor("cTemp", "Temperature", _colTemp);
  h += uiColor("cCond", "Condition (first word of the scrolling line)", _colCond);
  h += uiColor("cDet", "Scrolling details (feels like, humidity, wind)", _colDetail);
  h += uiColor("cAcc", "Small labels (unit letter, city name)", _colAccent);
  return h;
}

void WeatherModule::onSave(WebServer &server) {
  _celsius     = (server.arg("units") == "1");

  if (server.hasArg("city")) _city = cleanCity(server.arg("city"));
  String sla = server.arg("lat"), slo = server.arg("lon");
  float la = sla.toFloat(), lo = slo.toFloat();
  if (sla.length() > 0 && la >= -90 && la <= 90)   _lat = la;
  if (slo.length() > 0 && lo >= -180 && lo <= 180) _lon = lo;

  uint32_t c;
  if (uiReadColor(server, "cTemp", c)) _colTemp = c;
  if (uiReadColor(server, "cCond", c)) _colCond = c;
  if (uiReadColor(server, "cDet", c))  _colDetail = c;
  if (uiReadColor(server, "cAcc", c))  _colAccent = c;

  store.putBool("cel", _celsius);
  store.putFloat("lat", _lat);
  store.putFloat("lon", _lon);
  store.putString("city", _city);
  store.putUInt("cTemp", _colTemp);
  store.putUInt("cCond", _colCond);
  store.putUInt("cDet", _colDetail);
  store.putUInt("cAcc", _colAccent);

  _fetchNow = true;        // re-fetch with the new units / location
}

String WeatherModule::summary() {
  if (!_valid) {
    if (_status[0]) return String("No weather data yet - ") + _status + " (trying again)";
    return "No weather data yet";
  }
  return String((int)roundf(_temp)) + "\xC2\xB0" + tUnit() + ", " + wxConditionText(_code);
}

String WeatherModule::actionsHtml() {
  return "<form method='POST' action='/weather/refresh'><button class='sec' type='submit'>Refresh weather now</button></form>"
         "<form method='POST' action='/weather/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void WeatherModule::registerRoutes(WebServer &server) {
  server.on("/weather/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/weather");
  });
  server.on("/weather/colors-reset", HTTP_POST, [this]() {
    _colTemp = DEF_COL_TEMP; _colCond = DEF_COL_COND;
    _colDetail = DEF_COL_DETAIL; _colAccent = DEF_COL_ACCENT;
    store.putUInt("cTemp", _colTemp);
    store.putUInt("cCond", _colCond);
    store.putUInt("cDet", _colDetail);
    store.putUInt("cAcc", _colAccent);
    app.requestRedraw();
    webRedirect("/weather?saved=1");
  });
}

// ---------- data ----------
// Find a numeric field inside the "current":{...} block of the response.
static bool getField(const String &body, const char *key, float &out) {
  int cur = body.indexOf("\"current\":{");
  if (cur < 0) return false;
  String k = String("\"") + key + "\":";
  int i = body.indexOf(k, cur);
  if (i < 0) return false;
  out = body.substring(i + k.length()).toFloat();
  return true;
}

void WeatherModule::setStatus(const char *s) {
  strncpy(_status, s, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = 0;
}

bool WeatherModule::fetch() {
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return false; }

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(_lat, 4) +
               "&longitude=" + String(_lon, 4) +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
               "weather_code,wind_speed_10m,is_day"
               "&temperature_unit=" + (_celsius ? "celsius" : "fahrenheit") +
               "&wind_speed_unit=" + (_celsius ? "kmh" : "mph") +
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
    Serial.print("Weather: ");
    Serial.println(_status);
    return false;
  }
  String body = http.getString();
  http.end();

  float t, f, h, w, c, d;
  if (!getField(body, "temperature_2m", t) ||
      !getField(body, "apparent_temperature", f) ||
      !getField(body, "relative_humidity_2m", h) ||
      !getField(body, "weather_code", c) ||
      !getField(body, "wind_speed_10m", w) ||
      !getField(body, "is_day", d)) {
    setStatus("the reply was not understood");
    return false;
  }
  _temp = t; _feels = f; _humidity = h; _wind = w;
  _code = (int)c; _isDay = (d > 0.5f);
  _valid = true;
  setStatus("");
  return true;
}

// ---------- background task ----------
void WeatherModule::begin() {
  _everFetched = false;
  netWorkerAdd(this);                                        // downloads run in the shared network task
}

void WeatherModule::netTick() {
  if (millis() < 1500) return;                               // let Wi-Fi settle after start-up
  if (!enabled()) return;                                    // only fetch while the function is switched on
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return; }   // not a failed try
  const unsigned long now = millis();
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
    Serial.println(ok ? "Weather updated" : "Weather fetch failed");
  }
}

// ---------- drawing ----------
// Icons are drawn inside roughly x 42..63, y 1..24
void WeatherModule::drawIcon() {
  drawWxIcon(gDisplay->width() - 9, 8, _code, _isDay, false);
}

static const unsigned long SCROLL_STEP = 25;         // ms per pixel of scrolling

// A new frame exactly when the text has moved one more pixel (redrawing on any other beat makes the
// text move one pixel, then two, then one ...).
bool WeatherModule::needsRedraw() {
  return _valid && (millis() - _scrollT0) / SCROLL_STEP != _lastFrame;
}

static void drawPortraitCardText(const String &text, int baseline, uint16_t color,
                                 unsigned long now, unsigned long scrollStart, unsigned long phase) {
  const int W = gDisplay->width();
  const int textW = (int)text.length() * 4 - 1;
  int x = (W - textW) / 2;
  if (textW > W - 2) {
    const int period = textW + 6;
    x = -(int)(((now - scrollStart) / SCROLL_STEP + phase) % period);
  }
  gDisplay->setClipX(0, W);
  gDisplay->setTextColor(color);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(text);
  if (textW > W - 2) {
    gDisplay->setCursor(x + textW + 6, baseline);
    gDisplay->print(text);
  }
  gDisplay->clearClip();
}

void WeatherModule::drawPortrait(unsigned long now) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const uint16_t footerBg = rgb565(0x101B25);
  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->fillRect(0, 58, W, H - 58, footerBg);
  const char *condition = wxConditionText(_code);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextSize(1);
  drawPortraitCardText(String(condition) + "   ", 6, rgb565(_colCond), now, _scrollT0, 0);

  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%d", (int)roundf(_temp));
  gDisplay->setFont(nullptr);
  gDisplay->setTextColor(rgb565(_colTemp));
  // Smooth font: a Helvetica-like face fitted to the box, antialiased, in place of the blown-up
  // 5x7 bitmap font. The degree mark (a plain drawn glyph, not part of any font) is placed right
  // after the number's actual measured width rather than an assumed fixed width.
  SmoothFit fit = fitSmoothFont(tbuf, W - 10, 13);
  if (fit.font) {
    const int totalW = (int)fit.w + 1 + 3;            // number + gap + the degree mark
    const int startX = (W - totalW) / 2, top = 12;
    gDisplay->setCursor(startX - fit.xBearing, top - fit.top);
    gDisplay->print(tbuf);
    gDisplay->antialiasText(startX, top, fit.w, fit.h, rgb565(_colTemp));
    drawWxDegree(startX + (int)fit.w + 1, top + 1, rgb565(_colTemp));
    gDisplay->setFont(nullptr);
  } else {
    gDisplay->setTextSize(2);
    const int tempW = (int)strlen(tbuf) * 12;
    gDisplay->setCursor((W - tempW) / 2, 13);
    gDisplay->print(tbuf);
    drawWxDegree((W + tempW) / 2 - 1, 14, rgb565(_colTemp));
    gDisplay->setTextSize(1);
  }
  drawWxIcon(W / 2, 41, _code, _isDay, true);

  char footer[96];
  snprintf(footer, sizeof(footer), "FEELS LIKE %d  WIND %d %s  HUMIDITY %d%%",
           (int)roundf(_feels), (int)roundf(_wind), wUnit(), (int)roundf(_humidity));
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextSize(1);
  drawPortraitCardText(footer, 60, rgb565(_colAccent), now, _scrollT0, 0);
  gDisplay->setFont(nullptr);
  gDisplay->flipDMABuffer();
}

// One screen: the big temperature and the picture, and a line along the bottom that scrolls the
// condition, feels-like temperature, humidity and wind.
void WeatherModule::drawPage(int sub) {
  (void)sub;
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  if (now - _lastDraw > 500) _scrollT0 = now;            // a fresh showing: the text enters from the right
  _lastDraw = now;
  _lastFrame = (now - _scrollT0) / SCROLL_STEP;           // the scroll step on screen

  if (gDisplay->height() > gDisplay->width()) {
    drawPortrait(now);
    return;
  }

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);
  gDisplay->fillRect(0, 26, W, H - 26, rgb565(0x101B25));

  const char *condition = wxConditionText(_code);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(rgb565(_colCond));
  const int conditionW = (int)strlen(condition) * 4 - 1;
  gDisplay->setCursor((W - conditionW) / 2, 6);
  gDisplay->print(condition);
  gDisplay->setFont(nullptr);

  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%d", (int)roundf(_temp));
  gDisplay->setTextColor(rgb565(_colTemp));
  SmoothFit fit = fitSmoothFont(tbuf, W - 20, 13);
  if (fit.font) {
    const int top = 8;
    gDisplay->setCursor(2 - fit.xBearing, top - fit.top);
    gDisplay->print(tbuf);
    gDisplay->antialiasText(2, top, fit.w, fit.h, rgb565(_colTemp));
    drawWxDegree(2 + (int)fit.w + 1, top + 1, rgb565(_colTemp));
    gDisplay->setFont(nullptr);
  } else {
    gDisplay->setTextSize(2);
    gDisplay->setCursor(2, 9);
    gDisplay->print(tbuf);
    drawWxDegree(1 + (int)strlen(tbuf) * 12, 10, rgb565(_colTemp));
    gDisplay->setTextSize(1);
  }

  drawWxIcon(W - 12, 14, _code, _isDay, true);

  char details[64];
  snprintf(details, sizeof(details), "FEELS LIKE %d  WIND %d %s  HUMIDITY %d%%",
           (int)roundf(_feels), (int)roundf(_wind), wUnit(), (int)roundf(_humidity));
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextSize(1);
  drawPortraitCardText(details, 29, rgb565(_colAccent), now, _scrollT0, 0);
  gDisplay->setFont(nullptr);
  gDisplay->flipDMABuffer();
}
