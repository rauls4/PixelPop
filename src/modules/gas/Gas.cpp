#include "Gas.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include <Fonts/TomThumb.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/SmoothFont.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../ticker/TickerData.h"               // Dechunker

static const uint32_t DEF_COL_PRICE = 0xFFFFFF;
static const uint32_t DEF_COL_UP    = 0xFF5040;      // a price that went up: red
static const uint32_t DEF_COL_DOWN  = 0x30E060;      // a price that went down: green
static const uint32_t DEF_COL_LABEL = 0x00C8FF;

static const unsigned long REFRESH_MS = 3UL * 60 * 60 * 1000;      // the page changes once a day; look every 3 hours
static const unsigned long RETRY_MS   = 5UL * 60 * 1000;
static const unsigned long QUICK_RETRY_MS[3] = {5000, 15000, 60000};
static const unsigned long SCROLL_STEP = 30;                       // ms per pixel of the bottom line

static const char *const GRADE_NAMES[GAS_GRADE_COUNT] = {"Regular", "Mid-grade", "Premium", "Diesel"};
static const char *const GRADE_SHORT[GAS_GRADE_COUNT] = {"REGULAR", "MID", "PREMIUM", "DIESEL"};
static const char *const WHEN_NAMES[GAS_WHEN_COUNT] = {"Now", "Yesterday", "Week", "Month", "Year"};

// ---------- settings ----------
void GasModule::onLoad() {
  _grade    = store.getU8("grade", GAS_REGULAR);
  if (_grade >= GAS_GRADE_COUNT) _grade = GAS_REGULAR;
  _colPrice = store.getUInt("cPr", DEF_COL_PRICE);
  _colUp    = store.getUInt("cUp", DEF_COL_UP);
  _colDown  = store.getUInt("cDn", DEF_COL_DOWN);
  _colLabel = store.getUInt("cLb", DEF_COL_LABEL);
}

String GasModule::onSettingsHtml() {
  String h;
  h += uiSection("Fuel");
  h += uiSelect("grade", "Show", GRADE_NAMES, GAS_GRADE_COUNT, _grade);
  h += "<p class='m'>The national average price of a gallon, from AAA (gasprices.aaa.com). It is updated once a day. "
       "A price that has gone up is shown in red, one that has gone down in green.</p>";
  h += uiSection("Colors");
  h += uiColor("cPr", "Price", _colPrice);
  h += uiColor("cUp", "Price went up", _colUp);
  h += uiColor("cDn", "Price went down", _colDown);
  h += uiColor("cLb", "Title and labels", _colLabel);
  if (_valid) {
    h += uiSection("Latest numbers");
    String t = "<p class='m'>";
    for (int i = 0; i < GAS_WHEN_COUNT; i++) {
      if (!_has[i]) continue;
      t += String(WHEN_NAMES[i]) + ": $" + String(_p[i], 4) + "<br>";
    }
    h += t + "</p>";
  } else if (_status[0]) {
    h += String("<p class='m'>No data yet: ") + _status + "</p>";
  }
  return h;
}

void GasModule::onSave(WebServer &server) {
  _grade = (uint8_t)uiReadLong(server, "grade", _grade, 0, GAS_GRADE_COUNT - 1);
  uint32_t c;
  if (uiReadColor(server, "cPr", c)) _colPrice = c;
  if (uiReadColor(server, "cUp", c)) _colUp = c;
  if (uiReadColor(server, "cDn", c)) _colDown = c;
  if (uiReadColor(server, "cLb", c)) _colLabel = c;
  store.putU8("grade", _grade);
  store.putUInt("cPr", _colPrice);
  store.putUInt("cUp", _colUp);
  store.putUInt("cDn", _colDown);
  store.putUInt("cLb", _colLabel);
  _fetchNow = true;                                           // the grade may have changed: read it again
}

String GasModule::actionsHtml() {
  return "<form method='POST' action='/gas/refresh'><button class='sec' type='submit'>Refresh gas prices now</button></form>"
         "<form method='POST' action='/gas/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void GasModule::registerRoutes(WebServer &server) {
  server.on("/gas/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/gas");
  });
  server.on("/gas/colors-reset", HTTP_POST, [this]() {
    _colPrice = DEF_COL_PRICE; _colUp = DEF_COL_UP; _colDown = DEF_COL_DOWN; _colLabel = DEF_COL_LABEL;
    store.putUInt("cPr", _colPrice);
    store.putUInt("cUp", _colUp);
    store.putUInt("cDn", _colDown);
    store.putUInt("cLb", _colLabel);
    app.requestRedraw();
    webRedirect("/gas?saved=1");
  });
}

String GasModule::summary() {
  if (!_valid) return _status[0] ? String("No data yet - ") + _status : String("No data yet");
  char ch[16] = "";
  if (_has[GAS_YESTERDAY]) { gasFmtChange(ch, sizeof(ch), _p[GAS_NOW] - _p[GAS_YESTERDAY]); }
  String s = String(GRADE_NAMES[_grade]) + " $" + String(_p[GAS_NOW], 3);
  if (ch[0]) s += String(" (") + ch + " today)";
  return s;
}

// ---------- data ----------
void GasModule::setStatus(const char *s) {
  strncpy(_status, s, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = 0;
}

bool GasModule::fetch() {
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return false; }
  WiFiClientSecure client;
  client.setInsecure();                                       // a public price table: no certificate check
  client.setTimeout(8);
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);                                       // a plain, un-chunked reply we can read as a stream
  http.setUserAgent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15");
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  if (!http.begin(client, "https://gasprices.aaa.com/")) { setStatus("could not start the request"); return false; }
  int status = http.GET();
  if (status != 200) {
    char tls[40] = "";
    client.lastError(tls, sizeof(tls));
    http.end();
    if (status > 0) { char m[48]; snprintf(m, sizeof(m), "the server answered %d", status); setStatus(m); }
    else netExplain(_status, sizeof(_status), status, "gasprices.aaa.com", tls);
    Serial.print("Gas: ");
    Serial.println(_status);
    return false;
  }

  const bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  Dechunker dc;
  dc.begin();
  static AaaParser parser;                                    // static: it is not small
  parser.begin();
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[512];
  unsigned long start = millis(), lastData = start;
  size_t total = 0;
  while (s && !parser.done() && total < 500000 && millis() - start < 30000) {
    int avail = s->available();
    if (avail > 0) {
      int n = s->read(buf, avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail);
      for (int i = 0; i < n && !parser.done(); i++) {
        if (!chunked || dc.feed((char)buf[i])) parser.feed((char)buf[i]);
      }
      total += n > 0 ? n : 0;
      lastData = millis();
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      if (!s->connected()) break;
      if (millis() - lastData > 8000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();

  if (!parser.has(GAS_NOW, _grade)) { setStatus("the price table was not found on the page"); return false; }
  for (int i = 0; i < GAS_WHEN_COUNT; i++) {
    _has[i] = parser.has(i, _grade);
    _p[i] = _has[i] ? parser.price(i, _grade) : 0;
  }
  _valid = true;
  _gotAt = millis();
  setStatus("");
  return true;
}

// ---------- background task ----------
void GasModule::begin() {
  _everFetched = false;
  netWorkerAdd(this);
}

void GasModule::netTick() {
  if (millis() < 9000) return;                                // let Wi-Fi and the first downloads settle
  if (!enabled()) return;
  if (WiFi.status() != WL_CONNECTED) { setStatus("waiting for Wi-Fi"); return; }
  const unsigned long now = millis();
  const unsigned long interval = _lastOk ? REFRESH_MS : (_fails < 3 ? QUICK_RETRY_MS[_fails] : RETRY_MS);
  if (_fetchNow || !_everFetched || now - _lastFetch >= interval) {
    _fetchNow = false;
    bool ok = false;
    if (netLockTake(60000)) {
      ok = fetch();
      netLockGive();
    } else setStatus("the network was busy");
    _lastOk = ok;
    if (ok) _fails = 0; else if (_fails < 250) _fails++;
    _lastFetch = millis();
    _everFetched = true;
    app.requestRedraw();
    Serial.println(ok ? "Gas prices updated" : "Gas prices fetch failed");
  }
}

// ---------- drawing ----------
bool GasModule::needsRedraw() {
  return _valid && (millis() - _scrollT0) / SCROLL_STEP != _lastStep;      // the line moved a pixel
}

// Layout: title / the big price ($ and two decimals) / the changes scrolling along the bottom.
void GasModule::drawPage(int sub) {
  (void)sub;
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  if (now - _lastDraw > 500) _scrollT0 = now;                 // a new showing: the text enters from the right
  _lastDraw = now;
  _lastStep = (now - _scrollT0) / SCROLL_STEP;

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);

  if (H > W) {
    const char *title = "GA$";
    const int titleW = (int)strlen(title) * 4 - 1;
    gDisplay->setFont(&TomThumb);
    gDisplay->setTextColor(rgb565(_colLabel));
    gDisplay->setCursor((W - titleW) / 2, 6);
    gDisplay->print(title);
    gDisplay->setFont(nullptr);

    const int mills = (int)floorf(_p[GAS_NOW] * 1000.0f + 0.5f);
    char dollars[8], cents[8];
    snprintf(dollars, sizeof(dollars), "%d", mills / 1000);
    snprintf(cents, sizeof(cents), ".%03d", mills % 1000);
    const int dollarW = 4;
    const int centsW = (int)strlen(cents) * 4 - 1;
    gDisplay->setTextColor(rgb565(_colPrice));
    // Smooth font for the dollars digit(s); the leading "$" and the small raised cents stay in
    // TomThumb either way, so only the middle width needs to come from the font actually fitted.
    SmoothFit fit = fitSmoothFont(dollars, W - dollarW - centsW - 2, 18);
    if (fit.font) {
      const int priceX = (W - dollarW - (int)fit.w - centsW) / 2;
      const int top = 10;
      gDisplay->setFont(&TomThumb);
      gDisplay->setTextColor(rgb565(_colLabel));
      gDisplay->setCursor(priceX, 21);
      gDisplay->print("$");
      gDisplay->setFont(fit.font);
      gDisplay->setTextColor(rgb565(_colPrice));
      gDisplay->setCursor(priceX + dollarW - fit.xBearing, top - fit.top);
      gDisplay->print(dollars);
      gDisplay->antialiasText(priceX + dollarW, top, fit.w, fit.h, rgb565(_colPrice));
      gDisplay->setFont(&TomThumb);
      gDisplay->setTextColor(rgb565(_colLabel));
      gDisplay->setCursor(priceX + dollarW + (int)fit.w, 21);
      gDisplay->print(cents);
      gDisplay->setFont(nullptr);
    } else {
      const int dollarsW = (int)strlen(dollars) * 12 - 1;
      const int priceX = (W - dollarW - dollarsW - centsW) / 2;
      gDisplay->setFont(&TomThumb);
      gDisplay->setTextColor(rgb565(_colLabel));
      gDisplay->setCursor(priceX, 21);
      gDisplay->print("$");
      gDisplay->setFont(nullptr);
      gDisplay->setTextColor(rgb565(_colPrice));
      gDisplay->setTextSize(2);
      gDisplay->setCursor(priceX + dollarW, 10);
      gDisplay->print(dollars);
      gDisplay->setTextSize(1);
      gDisplay->setFont(&TomThumb);
      gDisplay->setCursor(priceX + dollarW + dollarsW, 21);
      gDisplay->print(cents);
      gDisplay->setFont(nullptr);
    }

    const int chartTop = 30;
    const int chartBottom = H - 13;
    const int chartHeight = chartBottom - chartTop;
    const int history[] = {GAS_YEAR, GAS_MONTH, GAS_WEEK, GAS_YESTERDAY, GAS_NOW};
    float low = _p[GAS_NOW], high = _p[GAS_NOW];
    for (int i = 0; i < 5; i++) {
      if (!_has[history[i]]) continue;
      low = min(low, _p[history[i]]);
      high = max(high, _p[history[i]]);
    }
    if (high - low < 0.02f) { low -= 0.01f; high += 0.01f; }
    float previous = _p[GAS_NOW];
    for (int i = 3; i >= 0; i--) {
      if (_has[history[i]]) { previous = _p[history[i]]; break; }
    }
    const float latestChange = _p[GAS_NOW] - previous;
    const uint16_t chartColor = rgb565(latestChange > 0.0005f ? _colUp
                                     : (latestChange < -0.0005f ? _colDown : _colLabel));
    gDisplay->drawRect(0, chartTop - 1, W, chartHeight + 3, rgb565(_colLabel));
    int lastX = 0, lastY = 0;
    bool haveLast = false;
    for (int i = 0; i < 5; i++) {
      if (!_has[history[i]]) continue;
      const int x = 2 + i * (W - 5) / 4;
      const float amount = (_p[history[i]] - low) / (high - low);
      const int y = chartBottom - (int)(amount * chartHeight + 0.5f);
      if (haveLast) gDisplay->drawLine(lastX, lastY, x, y, chartColor);
      if (i != 4) gDisplay->fillCircle(x, y, 1, chartColor);
      lastX = x;
      lastY = y;
      haveLast = true;
    }

    struct Piece { char text[24]; uint32_t color; };
    Piece pieces[GAS_WHEN_COUNT - 1];
    int count = 0;
    int textW = 0;
    static const char *const label[] = {"DAY", "WEEK", "MONTH", "YEAR"};
    for (int i = GAS_YESTERDAY; i < GAS_WHEN_COUNT; i++) {
      if (!_has[i]) continue;
      const float delta = _p[GAS_NOW] - _p[i];
      char amount[16];
      gasFmtChange(amount, sizeof(amount), delta);
      snprintf(pieces[count].text, sizeof(pieces[count].text), "%s %s   ", label[i - GAS_YESTERDAY], amount);
      pieces[count].color = fabsf(delta) < 0.0005f ? _colLabel : (delta > 0 ? _colUp : _colDown);
      textW += (int)strlen(pieces[count].text) * 4;
      count++;
    }
    if (count > 0) {
      const int period = textW + 8;
      const int offset = (int)(((now - _scrollT0) / SCROLL_STEP) % (unsigned long)period);
      gDisplay->setClipX(0, W);
      gDisplay->setFont(&TomThumb);
      for (int copy = 0; copy < 2; copy++) {
        int x = W - offset + copy * period;
        for (int i = 0; i < count; i++) {
          gDisplay->setTextColor(rgb565(pieces[i].color));
          gDisplay->setCursor(x, H - 2);
          gDisplay->print(pieces[i].text);
          x += (int)strlen(pieces[i].text) * 4;
        }
      }
      gDisplay->setFont(nullptr);
      gDisplay->clearClip();
    }
    gDisplay->flipDMABuffer();
    return;
  }

  // title
  char title[24];
  snprintf(title, sizeof(title), "US %s", GRADE_SHORT[_grade]);
  gDisplay->setTextColor(rgb565(_colLabel));
  gDisplay->setCursor(2, 0);
  gDisplay->print(title);

  // price: $ 4.47
  const float price = _p[GAS_NOW];
  int cents = (int)floorf(price * 100.0f + 0.5f);              // 4.476 -> 448
  char big[20];
  snprintf(big, sizeof(big), "%d.%02d", cents / 100, cents % 100);
  const bool tall = H >= 26;
  const int bigY = 8;
  const int numY = bigY + 1;             // the number sits a pixel lower than the "$"
  gDisplay->setTextColor(rgb565(_colPrice));
  SmoothFit fit = tall ? fitSmoothFont(big, W - 16, H - 17) : SmoothFit{nullptr, 0, 0, 0, 0};
  if (fit.font) {
    const int totalW = 6 + (int)fit.w;
    int x = (W - totalW) / 2;
    if (x < 0) x = 0;
    gDisplay->setFont(nullptr);
    gDisplay->setTextColor(rgb565(_colLabel));
    gDisplay->setCursor(x, bigY);
    gDisplay->print("$");
    gDisplay->setFont(fit.font);
    gDisplay->setTextColor(rgb565(_colPrice));
    gDisplay->setCursor(x + 6 - fit.xBearing, numY - fit.top);
    gDisplay->print(big);
    gDisplay->antialiasText(x + 6, numY, fit.w, fit.h, rgb565(_colPrice));
    gDisplay->setFont(nullptr);
  } else {
    const int sz = tall ? 2 : 1;
    const int bigW = (int)strlen(big) * 6 * sz;
    const int totalW = 6 + bigW;
    int x = (W - totalW) / 2;
    if (x < 0) x = 0;
    gDisplay->setTextColor(rgb565(_colLabel));
    gDisplay->setCursor(x, bigY);
    gDisplay->print("$");
    gDisplay->setTextSize(sz);
    gDisplay->setTextColor(rgb565(_colPrice));
    gDisplay->setCursor(x + 6, numY);
    gDisplay->print(big);
    gDisplay->setTextSize(1);
  }

  // the changes, scrolling: an arrow, the time span, how much (dollars)
  struct Piece { char text[40]; uint32_t color; };
  Piece pcs[GAS_WHEN_COUNT - 1];
  int np = 0, textW = 0;
  for (int i = GAS_YESTERDAY; i < GAS_WHEN_COUNT; i++) {
    if (!_has[i]) continue;
    const float d = _p[GAS_NOW] - _p[i];
    char ch[16];
    gasFmtChange(ch, sizeof(ch), d);
    const bool flat = fabsf(d) < 0.0005f;
    static const char *const LBL[GAS_WHEN_COUNT] = {"", "Day", "Week", "Month", "Year"};
    snprintf(pcs[np].text, sizeof(pcs[np].text), "%c %s %s   ", flat ? '=' : (d > 0 ? '\x1E' : '\x1F'), LBL[i], ch);
    pcs[np].color = flat ? _colLabel : (d > 0 ? _colUp : _colDown);
    textW += (int)strlen(pcs[np].text) * 6;
    np++;
  }
  if (np == 0) { gDisplay->flipDMABuffer(); return; }
  const int y = H - 7;
  const int off = (int)(((now - _scrollT0) / SCROLL_STEP) % (unsigned long)textW);
  gDisplay->setClipX(0, W);
  for (int copy = 0; copy < 2; copy++) {
    gDisplay->setCursor(W - off + copy * textW, y);
    for (int i = 0; i < np; i++) {
      gDisplay->setTextColor(rgb565(pcs[i].color));
      gDisplay->print(pcs[i].text);
    }
  }
  gDisplay->clearClip();
  gDisplay->flipDMABuffer();
}
