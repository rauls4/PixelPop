#include "Ticker.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"

static const uint32_t DEF_UP = 0x30E060, DEF_DOWN = 0xFF4040, DEF_SYM = 0xFFFFFF,
                      DEF_NEWS = 0xFFD200, DEF_FX = 0x00C8FF, DEF_LABEL = 0x8A93A6;

static const unsigned long RETRY_MS = 60UL * 1000;               // after a failed fetch
static const int  GAP_PX = 28;                                   // blank space between one lap of a row and the next
static const int  MAX_SYMBOLS = 15;

// choices shown on the settings page
static const float THRESH[6] = {0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f};
static const char *const THRESH_NAMES[6] = {"Every move", "0.5% or more", "1% or more", "2% or more", "3% or more", "5% or more"};
static const unsigned STOCK_MIN[3] = {5, 15, 30};
static const char *const STOCK_EVERY_NAMES[3] = {"Every 5 minutes", "Every 15 minutes", "Every 30 minutes"};
static const unsigned NEWS_MIN[4] = {10, 15, 30, 60};
static const char *const NEWS_EVERY_NAMES[4] = {"Every 10 minutes", "Every 15 minutes", "Every 30 minutes", "Every hour"};
static const char *const SPEED_NAMES[3] = {"Slow", "Normal", "Fast"};
static const int MS_PER_PX[3] = {50, 32, 20};
// Each kind of row scrolls at its own slightly different speed (percent of the chosen speed),
// so the rows drift against each other instead of moving in lockstep: stocks, headlines, currency.
static const int ROW_SPEED_PCT[3] = {100, 87, 113};
static const char *const STOCK_SRC_NAMES[2] = {"Stooq", "Yahoo Finance"};

struct NewsSource { const char *name; const char *url; };
static const NewsSource NEWS_SOURCES[] = {
  {"NPR News",             "https://feeds.npr.org/1001/rss.xml"},
  {"BBC World",            "https://feeds.bbci.co.uk/news/world/rss.xml"},
  {"BBC Business",         "https://feeds.bbci.co.uk/news/business/rss.xml"},
  {"BBC Technology",       "https://feeds.bbci.co.uk/news/technology/rss.xml"},
  {"Google News (US)",     "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"},
  {"My own feed (address below)", ""},
};
static const int NEWS_SOURCE_COUNT = 6;
static const char *const USER_AGENT = "Mozilla/5.0 (compatible; PixelPop-ESP32)";

// ---------- helpers ----------
// Uppercase, keep letters/digits and a few symbol characters, comma separated, at most maxItems items of maxLen.
static String cleanList(const String &in, int maxItems, int maxLen) {
  String out, cur;
  int items = 0;
  for (size_t i = 0; i <= in.length(); i++) {
    char c = i < in.length() ? in[i] : ',';
    if (c == ',' || c == ' ' || c == ';' || c == '\n' || c == '\t') {
      if (cur.length() > 0 && items < maxItems) {
        if (out.length()) out += ",";
        out += cur;
        items++;
      }
      cur = "";
    } else {
      char u = (char)toupper((unsigned char)c);
      bool ok = (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '.' || u == '^' || u == '-' || u == '=';
      if (ok && (int)cur.length() < maxLen) cur += u;
    }
  }
  return out;
}

static int splitList(const String &list, String *out, int maxOut) {
  int n = 0;
  int start = 0;
  while (start <= (int)list.length() && n < maxOut) {
    int c = list.indexOf(',', start);
    if (c < 0) c = list.length();
    String s = list.substring(start, c);
    if (s.length()) out[n++] = s;
    start = c + 1;
  }
  return n;
}

static bool httpGet(const String &url, String &body, size_t maxLen) {
  WiFiClientSecure client;
  client.setInsecure();                 // skips the certificate check; fine for a public data display
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(USER_AGENT);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) { Serial.printf("Ticker: %d from %s\n", code, url.c_str()); http.end(); return false; }
  int len = http.getSize();
  if (len > (int)maxLen) { http.end(); return false; }
  body = http.getString();
  http.end();
  return body.length() > 0 && body.length() <= maxLen;
}

// ---------- settings ----------
void TickerModule::onLoad() {
  _showStocks = store.getBool("stk", true);
  _showNews   = store.getBool("nws", true);
  _showFx     = store.getBool("fx", true);
  _labels     = store.getBool("lbl", true);
  _speed      = store.getU8("spd", 1);       if (_speed > 2) _speed = 1;
  _symbols    = cleanList(store.getString("sym", _symbols), MAX_SYMBOLS, 10);
  if (_symbols.length() == 0) _symbols = "AAPL,MSFT,NVDA,TSLA,AMZN";
  _stockSrc   = store.getU8("src", 1);       if (_stockSrc > 1) _stockSrc = 1;
  _thresh     = store.getU8("thr", 2);       if (_thresh > 5) _thresh = 2;
  _stockEvery = store.getU8("sev", 1);       if (_stockEvery > 2) _stockEvery = 1;
  _newsSrc    = store.getU8("nsr", 0);       if (_newsSrc >= NEWS_SOURCE_COUNT) _newsSrc = 0;
  _newsUrl    = store.getString("nurl", "");
  _newsCount  = store.getU8("ncnt", 10);     if (_newsCount < 3) _newsCount = 3; if (_newsCount > RssParser::MAX_ITEMS) _newsCount = RssParser::MAX_ITEMS;
  _newsEvery  = store.getU8("nev", 1);       if (_newsEvery > 3) _newsEvery = 1;
  _base       = cleanList(store.getString("base", "USD"), 1, 3);
  if (_base.length() != 3) _base = "USD";
  _targets    = cleanList(store.getString("cur", _targets), 8, 3);
  if (_targets.length() == 0) _targets = "EUR,GBP,JPY";
  _amount     = store.getUInt("amt", 1);     if (_amount < 1) _amount = 1; if (_amount > 1000000) _amount = 1000000;
  _colUp      = store.getUInt("cUp", DEF_UP);
  _colDown    = store.getUInt("cDn", DEF_DOWN);
  _colSym     = store.getUInt("cSym", DEF_SYM);
  _colNews    = store.getUInt("cNws", DEF_NEWS);
  _colFx      = store.getUInt("cFx", DEF_FX);
  _colLabel   = store.getUInt("cLbl", DEF_LABEL);
}

String TickerModule::onSettingsHtml() {
  String h;
  h += uiSection("Rows");
  h += uiCheckbox("stk", "Stock moves", _showStocks);
  h += uiCheckbox("nws", "Headlines", _showNews);
  h += uiCheckbox("fx", "Currency conversion", _showFx);
  h += uiCheckbox("lbl", "Small tag at the start of each row (MARKETS, NEWS, FX)", _labels);
  h += uiSelect("spd", "Scroll speed", SPEED_NAMES, 3, _speed);
  h += "<p class='m'>Rows that are switched off are left out and the others get more room. "
       "A row appears once its data has arrived.</p>";

  h += uiSection("Stock moves");
  h += uiText("sym", "Stocks to watch (symbols separated by commas, up to 15)", _symbols, 120);
  h += uiSelect("thr", "Significant move", THRESH_NAMES, 6, _thresh);
  h += uiSelect("sev", "Update", STOCK_EVERY_NAMES, 3, _stockEvery);
  h += uiSelect("src", "Data from", STOCK_SRC_NAMES, 2, _stockSrc);
  h += "<p class='m'>Only stocks that moved at least that much are shown, biggest first. If fewer than three "
       "did, the three biggest movers are shown anyway. Prices are the latest the free service has, "
       "so outside market hours they show the last session. With Stooq the change is measured from the day's "
       "open; with Yahoo Finance it is measured from the previous close. US stocks need only the symbol "
       "(AAPL); for other markets use the exchange suffix of the service (for example VOD.UK on Stooq, VOD.L on Yahoo). "
       "Stooq has asked for an account key since April 2026 and answers 'not found' without one, so Yahoo Finance is the "
       "default; if the chosen service gives nothing, the other one is tried.</p>";

  static const char *srcNames[NEWS_SOURCE_COUNT];
  for (int i = 0; i < NEWS_SOURCE_COUNT; i++) srcNames[i] = NEWS_SOURCES[i].name;
  h += uiSection("Headlines");
  h += uiSelect("nsr", "News feed", srcNames, NEWS_SOURCE_COUNT, _newsSrc);
  h += uiText("nurl", "Address of your own RSS feed (must start with https://)", _newsUrl, 120);
  h += uiNumber("ncnt", "Number of headlines", _newsCount, 3, RssParser::MAX_ITEMS);
  h += uiSelect("nev", "Update", NEWS_EVERY_NAMES, 4, _newsEvery);

  h += uiSection("Currency conversion");
  h += uiText("base", "Convert from (3-letter code)", _base, 3);
  h += uiText("cur", "Convert to (codes separated by commas, up to 8)", _targets, 40);
  h += uiNumber("amt", "Amount", (long)_amount, 1, 1000000);
  h += "<p class='m'>Rates are the daily rates from open.er-api.com, refreshed every hour. "
       "Use standard currency codes such as USD, EUR, GBP, JPY, CAD, MXN.</p>";

  h += uiSection("Colors");
  h += uiColor("cUp", "Stock up", _colUp);
  h += uiColor("cDn", "Stock down", _colDown);
  h += uiColor("cSym", "Stock symbol", _colSym);
  h += uiColor("cNws", "Headlines", _colNews);
  h += uiColor("cFx", "Currency values", _colFx);
  h += uiColor("cLbl", "Tags and labels", _colLabel);
  return h;
}

void TickerModule::onSave(WebServer &server) {
  _showStocks = server.hasArg("stk");
  _showNews   = server.hasArg("nws");
  _showFx     = server.hasArg("fx");
  _labels     = server.hasArg("lbl");
  _speed      = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  String s = cleanList(server.arg("sym"), MAX_SYMBOLS, 10);
  if (s.length()) _symbols = s;
  _thresh     = (uint8_t)uiReadLong(server, "thr", _thresh, 0, 5);
  _stockEvery = (uint8_t)uiReadLong(server, "sev", _stockEvery, 0, 2);
  _stockSrc   = (uint8_t)uiReadLong(server, "src", _stockSrc, 0, 1);
  _newsSrc    = (uint8_t)uiReadLong(server, "nsr", _newsSrc, 0, NEWS_SOURCE_COUNT - 1);
  String u = server.arg("nurl");
  u.trim();
  _newsUrl = u.startsWith("https://") ? u : String("");
  _newsCount  = (uint8_t)uiReadLong(server, "ncnt", _newsCount, 3, RssParser::MAX_ITEMS);
  _newsEvery  = (uint8_t)uiReadLong(server, "nev", _newsEvery, 0, 3);
  String b = cleanList(server.arg("base"), 1, 3);
  if (b.length() == 3) _base = b;
  String t = cleanList(server.arg("cur"), 8, 3);
  if (t.length()) _targets = t;
  _amount     = (uint32_t)uiReadLong(server, "amt", _amount, 1, 1000000);

  uint32_t c;
  if (uiReadColor(server, "cUp", c))  _colUp = c;
  if (uiReadColor(server, "cDn", c))  _colDown = c;
  if (uiReadColor(server, "cSym", c)) _colSym = c;
  if (uiReadColor(server, "cNws", c)) _colNews = c;
  if (uiReadColor(server, "cFx", c))  _colFx = c;
  if (uiReadColor(server, "cLbl", c)) _colLabel = c;

  store.putBool("stk", _showStocks);
  store.putBool("nws", _showNews);
  store.putBool("fx", _showFx);
  store.putBool("lbl", _labels);
  store.putU8("spd", _speed);
  store.putString("sym", _symbols);
  store.putU8("thr", _thresh);
  store.putU8("sev", _stockEvery);
  store.putU8("src", _stockSrc);
  store.putU8("nsr", _newsSrc);
  store.putString("nurl", _newsUrl);
  store.putU8("ncnt", _newsCount);
  store.putU8("nev", _newsEvery);
  store.putString("base", _base);
  store.putString("cur", _targets);
  store.putUInt("amt", _amount);
  store.putUInt("cUp", _colUp);
  store.putUInt("cDn", _colDown);
  store.putUInt("cSym", _colSym);
  store.putUInt("cNws", _colNews);
  store.putUInt("cFx", _colFx);
  store.putUInt("cLbl", _colLabel);
  _fetchNow = true;                                  // new choices: fetch again right away
}

String TickerModule::actionsHtml() {
  return "<form method='POST' action='/ticker/refresh'><button class='sec' type='submit'>Update the ticker now</button></form>"
         "<form method='POST' action='/ticker/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void TickerModule::registerRoutes(WebServer &server) {
  server.on("/ticker/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/ticker");
  });
  server.on("/ticker/colors-reset", HTTP_POST, [this]() {
    _colUp = DEF_UP; _colDown = DEF_DOWN; _colSym = DEF_SYM; _colNews = DEF_NEWS; _colFx = DEF_FX; _colLabel = DEF_LABEL;
    store.putUInt("cUp", _colUp);
    store.putUInt("cDn", _colDown);
    store.putUInt("cSym", _colSym);
    store.putUInt("cNws", _colNews);
    store.putUInt("cFx", _colFx);
    store.putUInt("cLbl", _colLabel);
    app.requestRedraw();
    webRedirect("/ticker?saved=1");
  });
}

String TickerModule::summary() {
  static const char *const names[3] = {"Stocks", "Headlines", "Currency"};
  const bool on[3] = {_showStocks, _showNews, _showFx};
  String s;
  for (int i = 0; i < 3; i++) {
    if (!on[i]) continue;
    if (s.length()) s += " · ";
    s += names[i];
    if (_count[i] > 0) s += ": " + String(_count[i]);
    else s += _failed[i] ? ": could not load" : ": loading";
  }
  return s.length() ? s : String("All rows are switched off");
}

// ---------- background task ----------
void TickerModule::begin() {
  netWorkerAdd(this);                                   // downloads run in the shared network task
}

void TickerModule::netTick() {
  if (millis() < 8000) return;                          // let the flight tracker and the clock settle first
  if (!enabled()) { _tried[0] = _tried[1] = _tried[2] = false; return; }
  bool force = _fetchNow;
  _fetchNow = false;
  const bool on[3] = {_showStocks, _showNews, _showFx};
  for (int s = 0; s < 3; s++) {
    if (!on[s]) continue;
    unsigned long interval;
    if (_failed[s]) interval = RETRY_MS;
    else if (s == 0) interval = STOCK_MIN[_stockEvery] * 60000UL;
    else if (s == 1) interval = NEWS_MIN[_newsEvery] * 60000UL;
    else interval = 60UL * 60000UL;
    if (force || !_tried[s] || millis() - _lastTry[s] >= interval) fetchOne(s);
  }
}

void TickerModule::fetchOne(int which) {
  if (WiFi.status() != WL_CONNECTED) { _failed[which] = true; _tried[which] = true; _lastTry[which] = millis(); return; }
  if (!netLockTake(120000)) return;                     // another download is busy; try again soon
  bool ok = (which == 0) ? fetchStocks() : (which == 1) ? fetchNews() : fetchRates();
  netLockGive();
  _failed[which] = !ok;
  _tried[which] = true;
  _lastTry[which] = millis();
  Serial.printf("Ticker %s: %s\n", which == 0 ? "stocks" : which == 1 ? "news" : "currency", ok ? "updated" : "failed");
  app.requestRedraw();
}

// Swap a finished row in; the display only ever sees a complete row.
void TickerModule::publish(Row &dst, Row &src, int which, int count) {
  src.ready = true;
  if (_lock.take(2000)) {
    std::swap(dst, src);
    _count[which] = count;
    _lock.give();
  }
}

// ---- stocks ----
// All the symbols in one request. Returns how many quotes came back.
int TickerModule::quotesFromStooq(const String *syms, int ns, Quote *q) {
  String url = "https://stooq.com/q/l/?s=";
  for (int i = 0; i < ns; i++) {
    String s = syms[i];
    s.toLowerCase();
    if (s.indexOf('.') < 0 && s.charAt(0) != '^') s += ".us";        // US stocks by default
    if (i) url += ",";
    url += s;
  }
  url += "&f=sd2t2ohlcv&h&e=csv";
  String body;
  if (!httpGet(url, body, 6000)) return 0;
  return parseStooqCsv(body.c_str(), q, MAX_SYMBOLS);
}

// One request per symbol. Gives up early when the first few all fail (the service is refusing us).
int TickerModule::quotesFromYahoo(const String *syms, int ns, Quote *q) {
  int nq = 0, fails = 0;
  for (int i = 0; i < ns && (nq > 0 || fails < 3); i++) {
    String s = syms[i];
    if (s.length() > 3 && strcmp(s.c_str() + s.length() - 3, ".US") == 0) s.remove(s.length() - 3);   // Stooq's way of writing a US stock
    s.replace("^", "%5E");
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + s + "?interval=1d&range=1d";
    String body;
    Quote one;
    if (httpGet(url, body, 20000) && parseYahooChart(body.c_str(), one)) q[nq++] = one;
    else fails++;
  }
  return nq;
}
bool TickerModule::fetchStocks() {
  String list = _symbols;
  String syms[MAX_SYMBOLS];
  int ns = splitList(list, syms, MAX_SYMBOLS);
  if (ns == 0) return false;
  const float thr = THRESH[_thresh];

  // The chosen service first; if it gives nothing (Stooq wants an account key since April 2026 and
  // answers 404 without one), the other one.
  Quote q[MAX_SYMBOLS];
  int nq = 0;
  for (int attempt = 0; attempt < 2 && nq == 0; attempt++) {
    const int src = attempt == 0 ? _stockSrc : 1 - _stockSrc;
    if (src == 0) nq = quotesFromStooq(syms, ns, q);
    else          nq = quotesFromYahoo(syms, ns, q);
    if (nq == 0) Serial.printf("Ticker: no quotes from %s\n", STOCK_SRC_NAMES[src]);
  }
  if (nq == 0) return false;

  // order by size of the move, biggest first
  int idx[MAX_SYMBOLS];
  for (int i = 0; i < nq; i++) idx[i] = i;
  for (int i = 1; i < nq; i++) {
    int k = idx[i], j = i - 1;
    while (j >= 0 && fabsf(q[idx[j]].pct) < fabsf(q[k].pct)) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = k;
  }
  int show = 0;
  for (int i = 0; i < nq; i++) if (fabsf(q[idx[i]].pct) >= thr) show++;
  if (thr > 0 && show < 3) show = nq < 3 ? nq : 3;                    // never fewer than three
  if (thr == 0) show = nq;

  Row row;
  if (_labels) { row.add("MARKETS ", _colLabel); }
  for (int i = 0; i < show; i++) {
    const Quote &k = q[idx[i]];
    char pct[12];
    fmtPct(pct, sizeof(pct), k.pct);
    uint32_t c = k.pct >= 0.05f ? _colUp : (k.pct <= -0.05f ? _colDown : _colLabel);
    row.add(String(k.sym) + " ", _colSym);
    row.add(String(pct), c);
    row.add("    ", _colSym);
  }
  publish(_stocks, row, 0, show);
  return true;
}

// ---- headlines ----
bool TickerModule::fetchNews() {
  String url = (_newsSrc == NEWS_SOURCE_COUNT - 1) ? _newsUrl : String(NEWS_SOURCES[_newsSrc].url);
  if (url.length() == 0) return false;
  const int want = _newsCount;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);                     // plain, un-chunked reply we can read as a stream
  http.setUserAgent(USER_AGENT);
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  if (!http.begin(client, url)) return false;
  int status = http.GET();
  if (status != 200) { Serial.printf("Ticker: %d from %s\n", status, url.c_str()); http.end(); return false; }

  const bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  Dechunker dc;
  dc.begin();
  _rss.begin(want);
  WiFiClient *s = http.getStreamPtr();
  uint8_t buf[512];
  unsigned long start = millis(), lastData = start;
  while (s && !_rss.done() && millis() - start < 30000) {
    int avail = s->available();
    if (avail > 0) {
      int n = s->read(buf, avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail);
      for (int i = 0; i < n && !_rss.done(); i++) {
        if (!chunked || dc.feed((char)buf[i])) _rss.feed((char)buf[i]);
      }
      lastData = millis();
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      if (!s->connected()) break;
      if (millis() - lastData > 8000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  if (_rss.count() == 0) return false;

  Row row;
  if (_labels) row.add("NEWS ", _colLabel);
  for (int i = 0; i < _rss.count(); i++) {
    if (i) row.add("  |  ", _colLabel);
    row.add(String(_rss.title(i)), _colNews);
  }
  row.add("    ", _colNews);
  publish(_news, row, 1, _rss.count());
  return true;
}

// ---- currency ----
bool TickerModule::fetchRates() {
  String base = _base, list = _targets;
  const uint32_t amount = _amount;
  String body;
  if (!httpGet("https://open.er-api.com/v6/latest/" + base, body, 16000)) return false;
  char res[16] = "";
  jsonString(body.c_str(), "result", res, sizeof(res));
  if (strcmp(res, "success") != 0) return false;

  String codes[8];
  int nc = splitList(list, codes, 8);
  Row row;
  if (_labels) row.add(String(amount) + " " + base + " = ", _colLabel);
  int shown = 0;
  for (int i = 0; i < nc; i++) {
    double r;
    if (!jsonNumber(body.c_str(), codes[i].c_str(), r)) continue;
    char v[16];
    fmtRate(v, sizeof(v), r * amount);
    row.add(String(v) + " ", _colFx);
    row.add(codes[i], _colLabel);
    row.add("    ", _colFx);
    shown++;
  }
  if (shown == 0) return false;
  publish(_rates, row, 2, shown);
  return true;
}

// ---------- drawing ----------
int TickerModule::msPerPixel() const { return MS_PER_PX[_speed]; }

bool TickerModule::pageAvailable(int sub) {
  (void)sub;
  return (_showStocks && _stocks.ready) || (_showNews && _news.ready) || (_showFx && _rates.ready);
}

// Pixels a row has travelled: 64-bit so it never wraps around (millis() * percent overflows 32 bits in hours).
uint64_t TickerModule::rowPixels(int kind) const {
  return (uint64_t)millis() * (uint64_t)ROW_SPEED_PCT[kind] / (100ULL * (uint64_t)msPerPixel());
}

// The sum of every visible row's pixel count changes exactly when some row has to move by a pixel, so a
// frame goes out on that beat and no other (any other beat makes rows move unevenly: one pixel, then two).
unsigned long TickerModule::stepKey() const {
  unsigned long k = 0;
  if (_showStocks && _stocks.ready) k += (unsigned long)rowPixels(0);
  if (_showNews && _news.ready)     k += (unsigned long)rowPixels(1);
  if (_showFx && _rates.ready)      k += (unsigned long)rowPixels(2);
  return k;
}

bool TickerModule::needsRedraw() {
  return stepKey() != _lastKey;
}

// One row, scrolling left and looping (the copies follow each other with a gap).
// onWhite: the row sits on a white band, so its colors are swapped for dark ones that read on white.
static uint32_t darkOnWhite(uint32_t c, uint32_t label, uint32_t up, uint32_t down) {
  if (c == label) return 0x5A6272;                 // tags and labels: mid grey
  if (c == up)    return 0x0A7A2A;                 // stock up: dark green
  if (c == down)  return 0xC01818;                 // stock down: dark red
  return 0x000000;                                 // everything else: black
}

void TickerModule::drawRow(const Row &r, int y, int width, long phase, int kind, bool onWhite) {
  const int period = r.width + GAP_PX;
  if (r.width <= 0 || period <= 0) return;
  uint64_t px = rowPixels(kind);
  long off = (long)((px + (uint64_t)phase) % (uint64_t)period);
  for (int base = -(int)off; base < width; base += period) {
    int x = base;
    for (size_t i = 0; i < r.segs.size(); i++) {
      const Seg &sg = r.segs[i];
      int w = (int)sg.text.length() * 6;
      if (x + w > 0 && x < width) {
        // Print only the characters that are on the screen: a long headline is hundreds of characters, and
        // drawing all of them (almost every pixel clipped away) made the frames slow and uneven.
        const int len = (int)sg.text.length();
        const int first = x < 0 ? (-x) / 6 : 0;
        int last = (width - x + 5) / 6;
        if (last > len) last = len;
        gDisplay->setTextColor(rgb565(onWhite ? darkOnWhite(sg.color, _colLabel, _colUp, _colDown) : sg.color));
        gDisplay->setCursor(x + first * 6, y);
        gDisplay->print(first == 0 && last == len ? sg.text : sg.text.substring(first, last));
      }
      x += w;
      if (x >= width) break;
    }
  }
}

void TickerModule::drawPage(int sub) {
  (void)sub;
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  if (_lock.take(100)) {
    const Row *rows[3];
    int kind[3];                                       // 0 stocks, 1 headlines, 2 currency
    int n = 0;
    if (_showStocks && _stocks.ready) { kind[n] = 0; rows[n++] = &_stocks; }
    if (_showNews && _news.ready)     { kind[n] = 1; rows[n++] = &_news; }
    if (_showFx && _rates.ready)      { kind[n] = 2; rows[n++] = &_rates; }
    if (n > 0) {
      const int W = gDisplay->width(), H = gDisplay->height();
      const int band = H / n;
      const bool landscape = W >= H;
      for (int i = 0; i < n; i++) {
        int y = i * band + (band - 7) / 2;
        if (landscape && n > 1) {                       // nudge landscape's rows down slightly for better vertical centering
          if (i == 0) y += 1;                            // top row
          else if (i == n - 1) y += 2;                   // bottom row
        }
        const bool white = (i == 1);                   // the second line: dark text on a white band
        if (white) {
          gDisplay->fillRect(0, i * band, W, band, rgb565(0xFFFFFF));
          y += 1;                                        // sits a pixel lower in its own white band
        }
        drawRow(*rows[i], y, W, kind[i] * 37, kind[i], white);
      }
    }
    _lock.give();
  }
  _lastKey = stepKey();
  gDisplay->flipDMABuffer();
}
