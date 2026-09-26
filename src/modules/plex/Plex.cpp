#include "Plex.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <string.h>
#include <TJpg_Decoder.h>
#include <Fonts/TomThumb.h>            // compact font for ratings and titles
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../ticker/TickerData.h"      // Dechunker, tickerCleanText

static const uint32_t DEF_TITLE = 0xFFFFFF, DEF_GENRE = 0x5CC8FF, DEF_RATING = 0xFFD060;

static const unsigned long QUICK_RETRY_MS[3] = {10000, 30000, 60000};
static const unsigned long RETRY_MS = 5UL * 60 * 1000;
static const unsigned long HOLD_MS = 1500;             // the title waits before it starts to scroll
static const int GAP_PX = 18;                          // empty space between the end of the title and its next round
static const size_t MAX_JSON_BYTES = 2UL * 1024 * 1024;
static const size_t MAX_THUMB_BYTES = 24UL * 1024;
static const int UNWATCHED_SCAN_COUNT = 50;
static const int PORTRAIT_THUMB_WIDTH = 32;
static const int PORTRAIT_THUMB_HEIGHT = 39;

static const unsigned EVERY_MIN[4] = {15, 30, 60, 180};
static const char *const EVERY_NAMES[4] = {"Every 15 minutes", "Every 30 minutes", "Every hour", "Every 3 hours"};
static const char *const SPEED_NAMES[3] = {"Slow", "Normal", "Fast"};

// ---------- settings ----------
void PlexModule::onLoad() {
  _url      = store.getString("url", "");
  _token    = store.getString("tok", "");
  _lib      = store.getString("lib", "");
  _count    = store.getU8("cnt", 6);
  if (_count < 1 || _count > MAXM) _count = 6;
  _showCrit = store.getBool("cr", true);
  _showAud  = store.getBool("au", true);
  _showNew  = store.getBool("new", true);
  _showGenre = store.getBool("gen", true);
  _onlyUnwatched = store.getBool("unwatched", true);
  _speed    = store.getU8("spd", 1);
  if (_speed > 2) _speed = 1;
  _everyIdx = store.getU8("ev", 1);
  if (_everyIdx > 3) _everyIdx = 1;
  _colTitle  = store.getUInt("cTtl", DEF_TITLE);
  _colGenre  = store.getUInt("cGen", DEF_GENRE);
  _colRating = store.getUInt("cRat", DEF_RATING);
}

String PlexModule::onSettingsHtml() {
  String h;
  h += uiSection("Plex server");
  h += uiText("url", "Server address, for example http://192.168.1.20:32400", _url, 100);
  h += uiText("tok", "Plex token", _token, 60);
  h += "<p class='m'>The address is the one you use to open Plex on your own network, with the port number (32400 unless you changed it). "
       "The panel talks to the server directly, so it has to be on the same network. "
       "To find the token: in Plex on the web, open any movie, click the three dots, choose <b>Get Info</b>, then <b>View XML</b>. "
       "The address that opens ends with <b>X-Plex-Token=</b> followed by letters and digits: that is your token. "
       "Keep it private, it gives access to your server.</p>";
  h += uiText("lib", "Only this library (leave empty for all movie libraries)", _lib, 40);

  h += uiSection("Show");
  h += uiNumber("cnt", "Number of movies (each one gets a page)", _count, 1, MAXM);
  h += uiCheckbox("cr", "Critic rating (Rotten Tomatoes tomato)", _showCrit);
  h += uiCheckbox("au", "Audience rating (popcorn)", _showAud);
  h += uiCheckbox("gen", "Genre (the kind of film, e.g. Drama)", _showGenre);
  h += uiCheckbox("unwatched", "Only show movies that have not been watched", _onlyUnwatched);
  h += uiCheckbox("new", "Green corner for movies not watched yet", _showNew);
  h += uiSelect("spd", "Scroll speed for a long title", SPEED_NAMES, 3, _speed);
  h += uiSelect("ev", "Update", EVERY_NAMES, 4, _everyIdx);
  h += "<p class='m'>The genre and the ratings are the ones your server has stored for each movie, and the little picture says where each rating comes from: "
       "a tomato or popcorn bucket for Rotten Tomatoes, a yellow block for IMDb, a blue one for TMDb. "
       "A movie the server has no rating for simply shows none. When only unwatched movies are selected, the green corner is hidden because every movie already meets that condition.</p>";

  h += uiSection("Colors");
  h += uiColor("cTtl", "Title", _colTitle);
  h += uiColor("cGen", "Genre", _colGenre);
  h += uiColor("cRat", "Rating numbers", _colRating);

  char st[112];
  if (_lock.take(100)) { strncpy(st, _status, sizeof(st) - 1); st[sizeof(st) - 1] = 0; _lock.give(); } else st[0] = 0;
  if (st[0]) h += "<p class='m'>Last update: " + htmlEscape(String(st)) + "</p>";
  return h;
}

void PlexModule::onSave(WebServer &server) {
  String u = server.arg("url"); u.trim(); _url = u;
  String t = server.arg("tok"); t.trim(); _token = t;
  String l = server.arg("lib"); l.trim(); _lib = l;
  _count    = (uint8_t)uiReadLong(server, "cnt", _count, 1, MAXM);
  _showCrit = server.hasArg("cr");
  _showAud  = server.hasArg("au");
  _showNew  = server.hasArg("new");
  _showGenre = server.hasArg("gen");
  _onlyUnwatched = server.hasArg("unwatched");
  _speed    = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _everyIdx = (uint8_t)uiReadLong(server, "ev", _everyIdx, 0, 3);
  uint32_t c;
  if (uiReadColor(server, "cTtl", c)) _colTitle = c;
  if (uiReadColor(server, "cGen", c)) _colGenre = c;
  if (uiReadColor(server, "cRat", c)) _colRating = c;

  store.putString("url", _url);
  store.putString("tok", _token);
  store.putString("lib", _lib);
  store.putU8("cnt", _count);
  store.putBool("cr", _showCrit);
  store.putBool("au", _showAud);
  store.putBool("new", _showNew);
  store.putBool("gen", _showGenre);
  store.putBool("unwatched", _onlyUnwatched);
  store.putU8("spd", _speed);
  store.putU8("ev", _everyIdx);
  store.putUInt("cTtl", _colTitle);
  store.putUInt("cGen", _colGenre);
  store.putUInt("cRat", _colRating);
  _fetchNow = true;
  _lastOffset = -1;
}

String PlexModule::actionsHtml() {
  return "<form method='POST' action='/plex/refresh'><button class='sec' type='submit'>Update the movies now</button></form>"
         "<form method='POST' action='/plex/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void PlexModule::registerRoutes(WebServer &server) {
  server.on("/plex/refresh", HTTP_POST, [this]() {
    _fetchNow = true;
    webRedirect("/plex");
  });
  server.on("/plex/colors-reset", HTTP_POST, [this]() {
    _colTitle = DEF_TITLE; _colGenre = DEF_GENRE; _colRating = DEF_RATING;
    store.putUInt("cTtl", _colTitle);
    store.putUInt("cGen", _colGenre);
    store.putUInt("cRat", _colRating);
    app.requestRedraw();
    webRedirect("/plex?saved=1");
  });
}

void PlexModule::setStatus(const char *s) {
  if (_lock.take(200)) { strncpy(_status, s, sizeof(_status) - 1); _status[sizeof(_status) - 1] = 0; _lock.give(); }
}

String PlexModule::summary() {
  if (_url.length() == 0 || _token.length() == 0) return "Enter the server address and token on its page";
  if (!_tried) return "Loading";
  if (_mvN == 0) {
    if (_failed) {
      char st[112] = "";
      if (_lock.take(50)) { strncpy(st, _status, sizeof(st) - 1); _lock.give(); }
      return String("Could not load: ") + st;
    }
    return "No movies found";
  }
  String s;
  if (_lock.take(50)) { s = String("Newest: ") + _mv[0].label; _lock.give(); }
  return s;
}

// ---------- talking to the server ----------
String PlexModule::baseUrl() const {
  String u = _url;
  u.trim();
  while (u.endsWith("/")) u.remove(u.length() - 1);
  if (!u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
  if (u.startsWith("http://")) {                              // no port given: Plex's own
    const int slash = u.indexOf('/', 7);
    const String host = slash < 0 ? u.substring(7) : u.substring(7, slash);
    if (host.indexOf(':') < 0) u = "http://" + host + ":32400" + (slash < 0 ? String("") : u.substring(slash));
  }
  return u;
}

// Fetches url and hands the body (chunks already removed) to sink. Returns the HTTP status, or a value <= 0 when there was none.
int PlexModule::httpGet(const String &url, void (*sink)(void *, const uint8_t *, size_t), void *ctx, size_t maxBytes) {
  WiFiClientSecure secure;
  secure.setInsecure();                                       // the server's certificate is for plex.direct, not its address
  WiFiClient plain;
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  http.setUserAgent("PixelPop-ESP32");
  const char *keep[] = {"Transfer-Encoding"};
  http.collectHeaders(keep, 1);
  const bool ok = url.startsWith("https://") ? http.begin(secure, url) : http.begin(plain, url);
  if (!ok) return -1;
  http.addHeader("X-Plex-Token", _token);
  http.addHeader("X-Plex-Client-Identifier", "pixelpop-panel");
  http.addHeader("X-Plex-Product", "PixelPop");
  http.addHeader("Accept", "application/json");
  const int status = http.GET();
  if (status != 200) { http.end(); return status; }

  const bool chunked = http.header("Transfer-Encoding").indexOf("chunked") >= 0;
  Dechunker dc;
  dc.begin();
  WiFiClient *s = http.getStreamPtr();
  uint8_t raw[512], out[512];
  size_t total = 0;
  unsigned long start = millis(), lastData = start;
  bool overflow = false;
  while (s && millis() - start < 60000UL) {
    const int avail = s->available();
    if (avail > 0) {
      const int n = s->read(raw, avail > (int)sizeof(raw) ? (int)sizeof(raw) : avail);
      size_t k = 0;
      for (int i = 0; i < n; i++)
        if (!chunked || dc.feed((char)raw[i])) out[k++] = raw[i];
      total += k;
      if (total > maxBytes) { overflow = true; break; }
      if (k) sink(ctx, out, k);
      lastData = millis();
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      if (!s->connected()) break;
      if (millis() - lastData > 8000) break;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  http.end();
  return overflow ? -2 : 200;
}

struct JsonSink { PlexJson *j; };
static void jsonSink(void *ctx, const uint8_t *d, size_t n) {
  PlexJson *j = ((JsonSink *)ctx)->j;
  for (size_t i = 0; i < n; i++) j->feed((char)d[i]);
}

struct SectionCtx { char keys[6][12]; int n; const char *only; };
static bool sameText(const char *a, const char *b) { return strcasecmp(a, b) == 0; }
static void onSection(void *c, const PlexItem &it) {
  SectionCtx *s = (SectionCtx *)c;
  if (strcmp(it.type, "movie") != 0 || !it.key[0] || s->n >= 6) return;
  if (s->only[0]) {
    char clean[96];
    tickerCleanText(it.title, clean, sizeof(clean));
    if (!sameText(clean, s->only)) return;
  }
  strncpy(s->keys[s->n], it.key, sizeof(s->keys[0]) - 1);
  s->keys[s->n][sizeof(s->keys[0]) - 1] = 0;
  s->n++;
}

struct MovieCtx { PlexItem *best; int n; int want; bool onlyUnwatched; };
static void onMovie(void *c, const PlexItem &it) {
  MovieCtx *m = (MovieCtx *)c;
  if (strcmp(it.type, "movie") != 0 || !it.ratingKey[0] || !it.title[0]) return;
  if (m->onlyUnwatched && it.viewCount > 0) return;
  for (int i = 0; i < m->n; i++) if (!strcmp(m->best[i].ratingKey, it.ratingKey)) return;   // already have it
  int pos = 0;
  while (pos < m->n && m->best[pos].added >= it.added) pos++;   // newest first
  if (pos >= m->want) return;
  int last = m->n < m->want ? m->n : m->want - 1;
  for (int j = last; j > pos; j--) m->best[j] = m->best[j - 1];
  m->best[pos] = it;
  if (m->n < m->want) m->n++;
}

// ---------- background task ----------
void PlexModule::begin() {
  netWorkerAdd(this);
}

void PlexModule::netTick() {
  if (millis() < 16000) return;
  if (!enabled() || _url.length() == 0 || _token.length() == 0) { _tried = false; return; }
  if (time(nullptr) < 1700000000) return;                    // the clock is needed for "added 3 days ago"
  const bool force = _fetchNow;
  _fetchNow = false;
  const unsigned long interval = _failed ? (_fails <= 3 ? QUICK_RETRY_MS[_fails - 1 < 0 ? 0 : _fails - 1] : RETRY_MS) : EVERY_MIN[_everyIdx] * 60000UL;
  if (force || !_tried || millis() - _lastTry >= interval) {
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED && netLockTake(120000)) {
      ok = fetch();
      netLockGive();
    } else setStatus("waiting for the network");
    _failed = !ok;
    _fails = ok ? 0 : _fails + 1;
    _tried = true;
    _lastTry = millis();
    app.requestRedraw();
  }
}

bool PlexModule::fetch() {
  _stage = (Movie *)calloc(MAXM, sizeof(Movie));
  if (!_stage) { setStatus("not enough memory"); return false; }
  const bool ok = fetchInner();
  if (!ok) freeThumbnails(_stage, MAXM);
  free(_stage);
  _stage = nullptr;
  return ok;
}

struct ThumbCtx {
  uint8_t *data = nullptr;
  size_t size = 0;
  size_t capacity = 0;
  bool failed = false;
};

static void thumbSink(void *ctx, const uint8_t *data, size_t size) {
  ThumbCtx *thumb = (ThumbCtx *)ctx;
  if (thumb->failed || size > MAX_THUMB_BYTES - thumb->size) { thumb->failed = true; return; }
  const size_t needed = thumb->size + size;
  if (needed > thumb->capacity) {
    const size_t capacity = min(MAX_THUMB_BYTES, max(needed, thumb->capacity ? thumb->capacity * 2 : (size_t)2048));
    uint8_t *next = (uint8_t *)realloc(thumb->data, capacity);
    if (!next) { thumb->failed = true; return; }
    thumb->data = next;
    thumb->capacity = capacity;
  }
  memcpy(thumb->data + thumb->size, data, size);
  thumb->size += size;
}

bool PlexModule::fetchThumbnail(const char *path, uint8_t *&data, size_t &size) {
  data = nullptr;
  size = 0;
  if (!path || !path[0]) return false;
  ThumbCtx thumb;
  const String url = baseUrl() + "/photo/:/transcode?width=" + String(PORTRAIT_THUMB_WIDTH) +
                     "&height=" + String(PORTRAIT_THUMB_HEIGHT) + "&minSize=1&upscale=1&url=" + String(path);
  const int status = httpGet(url, thumbSink, &thumb, MAX_THUMB_BYTES);
  if (status != 200 || thumb.failed || thumb.size == 0) {
    free(thumb.data);
    return false;
  }
  data = thumb.data;
  size = thumb.size;
  return true;
}

void PlexModule::freeThumbnails(Movie *movies, int count) {
  for (int i = 0; i < count; i++) {
    free(movies[i].thumb);
    movies[i].thumb = nullptr;
    movies[i].thumbSize = 0;
  }
}

bool PlexModule::fetchInner() {
  const String base = baseUrl();
  // 1) the movie libraries
  SectionCtx sc;
  sc.n = 0; sc.only = _lib.c_str();
  PlexJson pj;
  JsonSink js = {&pj};
  pj.begin(onSection, &sc);
  int st = httpGet(base + "/library/sections", jsonSink, &js, MAX_JSON_BYTES);
  if (st != 200) {
    char msg[110], host[48];
    strncpy(host, base.c_str() + (base.startsWith("https") ? 8 : 7), sizeof(host) - 1);
    host[sizeof(host) - 1] = 0;
    if (st == 401) snprintf(msg, sizeof(msg), "the server did not accept the token (401)");
    else if (st > 0) snprintf(msg, sizeof(msg), "the server answered %d", st);
    else netExplain(msg, sizeof(msg), st, host, "");
    Serial.printf("Plex: sections: %s\n", msg);
    setStatus(msg);
    return false;
  }
  if (!pj.sawContainer()) { setStatus("that address did not answer like a Plex server"); return false; }
  if (sc.n == 0) { setStatus(_lib.length() ? "no movie library with that name" : "the server has no movie library"); return false; }

  // 2) the newest movies of each library, merged
  PlexItem *best = (PlexItem *)malloc(sizeof(PlexItem) * MAXM);
  if (!best) { setStatus("out of memory"); return false; }
  MovieCtx mc = {best, 0, _count, _onlyUnwatched};
  for (int i = 0; i < sc.n; i++) {
    pj.begin(onMovie, &mc);
    const int scanCount = _onlyUnwatched ? UNWATCHED_SCAN_COUNT : _count;
    st = httpGet(base + "/library/sections/" + sc.keys[i] + "/recentlyAdded?X-Plex-Container-Start=0&X-Plex-Container-Size=" + String(scanCount),
                 jsonSink, &js, MAX_JSON_BYTES);
    if (st != 200) {
      char msg[64];
      snprintf(msg, sizeof(msg), "the server answered %d for a library", st);
      Serial.printf("Plex: %s\n", msg);
      if (mc.n == 0 && i == sc.n - 1) { setStatus(msg); free(best); return false; }
    }
  }

  // 3) keep what the display needs of each movie
  const int n = mc.n;
  for (int i = 0; i < n; i++) {
    Movie &m = _stage[i];
    memset(&m, 0, sizeof(m));
    const PlexItem &it = best[i];
    strncpy(m.rk, it.ratingKey, sizeof(m.rk) - 1);
    char clean[96];
    tickerCleanText(it.title, clean, sizeof(clean));
    if (it.year > 0) snprintf(m.label, sizeof(m.label), "%s (%d)", clean, it.year);
    else snprintf(m.label, sizeof(m.label), "%s", clean);
    tickerCleanText(it.genre, m.genre, sizeof(m.genre));
    m.hasCrit = it.hasRating && it.rating > 0; m.crit = it.rating; m.critKind = plexRatingKind(it.ratingImage);
    m.hasAud = it.hasAudience && it.audience > 0; m.aud = it.audience; m.audKind = plexRatingKind(it.audienceImage);
    if (m.critKind == PR_NONE) m.critKind = PR_OTHER;
    if (m.audKind == PR_NONE) m.audKind = PR_OTHER;
    m.added = it.added;
    m.unwatched = it.viewCount <= 0;
    fetchThumbnail(it.thumb, m.thumb, m.thumbSize);
  }
  free(best);

  // 4) swap in the new list
  if (!_lock.take(3000)) { setStatus("the display was busy, will try again"); return false; }
  freeThumbnails(_mv, _mvN);
  for (int i = 0; i < n; i++) _mv[i] = _stage[i];
  _mvN = n;
  _lock.give();

  char msg[112];
  snprintf(msg, sizeof(msg), "%d newest movies", n);
  setStatus(msg);
  Serial.printf("Plex: %s\n", msg);
  return true;
}

// ---------- drawing ----------
int PlexModule::pageCount() { return _mvN > 0 ? _mvN : 1; }

bool PlexModule::pageAvailable(int sub) { return sub >= 0 && sub < _mvN && sub < _count; }

unsigned long PlexModule::msPerPx() const {
  static const unsigned long MS[3] = {70, 45, 28};
  return MS[_speed > 2 ? 1 : _speed];
}

static int offsetAt(unsigned long t, int lap, unsigned long step, unsigned long total) {
  if (t < HOLD_MS || t >= total || lap <= 0) return 0;
  return (int)(((t - HOLD_MS) / step) % (unsigned long)lap);
}

bool PlexModule::needsRedraw() {
  const unsigned long now = millis();
  if (now - _lastSeen > 500) { _t0 = now; _lastOffset = -1; }       // it was not on the screen: a new showing
  _lastSeen = now;
  if (_scrollable) {
    if (now - _t0 >= _totalMs + 300) { _t0 = now; return true; }
    return offsetAt(now - _t0, _lapPx, _stepMs, _totalMs) != _lastOffset;
  }
  return false;
}

int PlexModule::pageProgress() {
  const unsigned long now = millis();
  if (!_scrollable) return 0;
  if (now - _lastSeen > 1000) return 0;
  return (now - _t0 >= _totalMs) ? 2 : 1;
}

// 7x7 pictures for where a rating comes from. Letters pick the color.
static uint16_t spriteColor(char c) {
  switch (c) {
    case 'R': return rgb565(0xFA320A);     // tomato red / bucket red
    case 'G': return rgb565(0x2E9E3E);     // stem
    case 'g': return rgb565(0x8CC800);     // rotten splat
    case 'Y': return rgb565(0xFFC800);
    case 'W': return rgb565(0xFFFFFF);
    case 'I': return rgb565(0xF5C518);     // IMDb yellow
    case 'K': return rgb565(0x101010);
    case 'T': return rgb565(0x01B4E4);     // TMDb blue
    case 'D': return rgb565(0x0D253F);
    case 'S': return rgb565(0xB0B0B0);
    default:  return 0;
  }
}

static const char *const SP_FRESH[7] = {"...G...", ".GGGGG.", "RRRGRRR", "RRRRRRR", "RRRRRRR", ".RRRRR.", "..RRR.."};
static const char *const SP_ROTTEN[7] = {"..g.g..", ".ggggg.", "ggg.ggg", "ggggggg", ".ggggg.", "g.ggg.g", "..g.g.."};
static const char *const SP_POP[7] = {".YWYWY.", "WYWYWYW", ".RWRWR.", ".RWRWR.", ".RWRWR.", ".RWRWR.", "..RRR.."};
static const char *const SP_SPILLED[7] = {"Y.W....", ".YW.R..", "..RWRW.", ".RWRWR.", "..WRWR.", "...RWR.", "....R.."};
static const char *const SP_IMDB[7] = {"IIIIIII", "IIKKKII", "IIIKIII", "IIIKIII", "IIIKIII", "IIKKKII", "IIIIIII"};
static const char *const SP_TMDB[7] = {"TTTTTTT", "TDDDDDT", "TTTDTTT", "TTTDTTT", "TTTDTTT", "TTTDTTT", "TTTTTTT"};
static const char *const SP_OTHER[7] = {"...S...", "..SSS..", "SSSSSSS", ".SSSSS.", "..SSS..", ".SS.SS.", ".S...S."};

static void drawSprite(int x, int y, PlexRatingKind k) {
  const char *const *rows = SP_OTHER;
  switch (k) {
    case PR_RT_FRESH: rows = SP_FRESH; break;
    case PR_RT_ROTTEN: rows = SP_ROTTEN; break;
    case PR_POP_UP: rows = SP_POP; break;
    case PR_POP_SPILLED: rows = SP_SPILLED; break;
    case PR_IMDB: rows = SP_IMDB; break;
    case PR_TMDB: rows = SP_TMDB; break;
    default: break;
  }
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 7; c++) {
      const uint16_t col = spriteColor(rows[r][c]);
      if (col) gDisplay->drawPixel(x + c, y + r, col);
    }
}

static bool drawPlexThumbnail(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *pixels) {
  const int W = gDisplay->width(), H = gDisplay->height();
  for (uint16_t row = 0; row < height; row++) {
    const int py = y + row;
    if (py < 0 || py >= H) continue;
    for (uint16_t col = 0; col < width; col++) {
      const int px = x + col;
      if (px >= 0 && px < W) gDisplay->drawPixel(px, py, pixels[row * width + col]);
    }
  }
  return true;
}

static void drawPortraitThumbnail(const uint8_t *data, size_t size, int width, int height) {
  if (height <= width || !data || size == 0) return;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(drawPlexThumbnail);
  TJpgDec.drawJpg((width - PORTRAIT_THUMB_WIDTH) / 2, height - PORTRAIT_THUMB_HEIGHT, data, size);
}

// Text in the normal font when it fits in maxW pixels, else in the tiny capitals font (4 pixels a letter),
// else cut off. Returns the width used.
static int drawFitted(int x, int y, int maxW, const char *text, uint16_t color) {
  char buf[48];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  const int len = (int)strlen(buf);
  gDisplay->setTextColor(color);
  if (len * 6 - 1 <= maxW) {
    gDisplay->setCursor(x, y);
    gDisplay->print(buf);
    return len * 6 - 1;
  }
  for (char *p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  int keep = len;
  while (keep > 0 && keep * 4 - 1 > maxW) keep--;
  buf[keep] = 0;
  gDisplay->setFont(&TomThumb);
  gDisplay->setCursor(x, y + 6);
  gDisplay->print(buf);
  gDisplay->setFont(nullptr);
  return keep * 4 - 1;
}

void PlexModule::drawPage(int sub) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  if (sub != _curSub) { _curSub = sub; _t0 = now; }
  _lastSeen = now;
  _lastOffset = 0;
  _scrollable = false;

  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  if (sub < 0 || sub >= _mvN || !_lock.take(300)) { gDisplay->flipDMABuffer(); return; }
  const Movie &m = _mv[sub];

  // a green dog ear in the top left corner: not watched yet
  const bool tag = _showNew && !_onlyUnwatched && m.unwatched;
  if (tag) {
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 7 - y; x++) gDisplay->drawPixel(x, y, rgb565(x + y == 6 ? 0x8CFFA8 : 0x18B048));
  }
  const int x0 = tag ? 8 : 0, avail = W - x0;

  // ---- title: fixed if it fits, else scrolling round and round ----
  const int tw = (int)strlen(m.label) * 6 - 1;
  gDisplay->setTextColor(rgb565(_colTitle));
  if (tw <= avail) {
    gDisplay->setCursor(x0, 0);
    gDisplay->print(m.label);
  } else {
    _scrollable = true;
    _stepMs = msPerPx();
    _lapPx = tw + GAP_PX;
    const unsigned long lapMs = (unsigned long)_lapPx * _stepMs;
    const unsigned long dwell = seconds() * 1000UL;
    unsigned long laps = dwell > HOLD_MS ? (dwell - HOLD_MS + lapMs - 1) / lapMs : 1;
    if (laps < 1) laps = 1;
    _totalMs = HOLD_MS + laps * lapMs;
    const int off = offsetAt(now - _t0, _lapPx, _stepMs, _totalMs);
    _lastOffset = off;
    gDisplay->setClipX(x0, W);
    gDisplay->setCursor(x0 - off, 0);
    gDisplay->print(m.label);
    gDisplay->setCursor(x0 - off + _lapPx, 0);
    gDisplay->print(m.label);
    gDisplay->clearClip();
  }

  // ---- genre: the kind of film ----
  if (_showGenre && m.genre[0]) {
    const bool portrait = H > W;
    const int categoryY = portrait ? 8 : 10;
    char g[40];
    strncpy(g, m.genre, sizeof(g) - 1);
    g[sizeof(g) - 1] = 0;
    if ((int)strlen(g) * 4 - 1 > W) { char *comma = strchr(g, ','); if (comma) *comma = 0; }     // two genres do not fit: the first one
    if (portrait) {
      gDisplay->setFont(&TomThumb);
      gDisplay->setTextColor(rgb565(_colGenre));
      gDisplay->setCursor(0, categoryY + 5);
      gDisplay->print(g);
      gDisplay->setFont(nullptr);
    } else {
      drawFitted(0, categoryY, W, g, rgb565(_colGenre));
    }
  }

  // ---- ratings, side by side ----
  struct Row { PlexRatingKind k; float v; };
  Row rows[2]; int nr = 0;
  if (_showCrit && m.hasCrit) rows[nr++] = {m.critKind, m.crit};
  if (_showAud && m.hasAud) rows[nr++] = {m.audKind, m.aud};
  const int ry = H > W ? 15 : 21;
  int x = 0;
  for (int i = 0; i < nr; i++) {
    char t[12];
    plexRatingText(rows[i].k, rows[i].v, t, sizeof(t));
    const int w = 9 + (int)strlen(t) * 6 - 1;
    if (i > 0 && x + w > W) break;
    drawSprite(x, ry, rows[i].k);
    gDisplay->setTextColor(rgb565(_colRating));
    gDisplay->setCursor(x + 9, ry);
    gDisplay->print(t);
    x += w + 5;
  }
  drawPortraitThumbnail(m.thumb, m.thumbSize, W, H);

  _lock.give();
  gDisplay->flipDMABuffer();
}
