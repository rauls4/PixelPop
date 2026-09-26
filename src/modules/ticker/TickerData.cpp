#include "TickerData.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <math.h>

// ---------------- text ----------------
namespace {

struct Out {
  char *buf; size_t cap, len;
  void put(char c) {
    if (len + 1 >= cap) return;
    if (c == ' ' && (len == 0 || buf[len - 1] == ' ')) return;          // no leading / doubled spaces
    buf[len++] = c;
  }
  void puts(const char *s) { while (*s) put(*s++); }
};

// 0xC0..0xFF folded to plain letters
const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

void addCodepoint(Out &o, uint32_t cp) {
  if (cp < 128) { o.put(cp < 32 ? ' ' : (char)cp); return; }
  switch (cp) {
    case 0xA0: case 0x2009: case 0x200A: case 0x202F: o.put(' '); return;
    case 0x2018: case 0x2019: case 0x201B: case 0x2032: case 0x02BC: o.put('\''); return;
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: o.put('"'); return;
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: o.put('-'); return;
    case 0x2026: o.puts("..."); return;
    case 0x2022: case 0xB7: case 0x25CF: o.put('*'); return;
    case 0x20AC: o.puts("EUR"); return;
    case 0xA3: o.put((char)0x9C); return;          // pound sign (in the panel font)
    case 0xA5: o.put((char)0x9D); return;          // yen sign
    case 0xA2: o.put((char)0x9B); return;          // cent sign
    case 0xB0: o.put('o'); return;                  // degree
    case 0xAB: case 0xBB: o.put('"'); return;
    case 0x141: o.put('L'); return;  case 0x142: o.put('l'); return;
    case 0x160: o.put('S'); return;  case 0x161: o.put('s'); return;
    case 0x10C: o.put('C'); return;  case 0x10D: o.put('c'); return;
    case 0x106: o.put('C'); return;  case 0x107: o.put('c'); return;
    case 0x17D: o.put('Z'); return;  case 0x17E: o.put('z'); return;
    case 0x179: case 0x17B: o.put('Z'); return;  case 0x17A: case 0x17C: o.put('z'); return;
    case 0x143: o.put('N'); return;  case 0x144: o.put('n'); return;
    case 0x15A: o.put('S'); return;  case 0x15B: o.put('s'); return;
    case 0x11E: o.put('G'); return;  case 0x11F: o.put('g'); return;
    case 0x130: o.put('I'); return;  case 0x131: o.put('i'); return;
    case 0x15E: o.put('S'); return;  case 0x15F: o.put('s'); return;
    default: break;
  }
  if (cp >= 0xC0 && cp <= 0xFF) { o.put(LATIN1[cp - 0xC0]); return; }
  // anything else (emoji, other alphabets) is dropped
}

}  // namespace

void tickerCleanText(const char *in, char *out, size_t cap) {
  Out o = {out, cap, 0};
  const unsigned char *p = (const unsigned char *)in;
  while (*p) {
    unsigned char c = *p;
    if (c == '&') {
      // &amp; &lt; &gt; &quot; &apos; &nbsp; &#39; &#x27; ...
      const unsigned char *q = p + 1;
      int n = 0;
      while (q[n] && q[n] != ';' && n < 10) n++;
      if (q[n] == ';' && n > 0) {
        char name[12];
        memcpy(name, q, n);
        name[n] = 0;
        long cp = -1;
        if (name[0] == '#') {
          cp = (name[1] == 'x' || name[1] == 'X') ? strtol(name + 2, nullptr, 16) : strtol(name + 1, nullptr, 10);
        } else if (!strcmp(name, "amp"))  cp = '&';
        else if (!strcmp(name, "lt"))     cp = '<';
        else if (!strcmp(name, "gt"))     cp = '>';
        else if (!strcmp(name, "quot"))   cp = '"';
        else if (!strcmp(name, "apos"))   cp = '\'';
        else if (!strcmp(name, "nbsp"))   cp = 0xA0;
        else if (!strcmp(name, "rsquo") || !strcmp(name, "lsquo")) cp = '\'';
        else if (!strcmp(name, "rdquo") || !strcmp(name, "ldquo")) cp = '"';
        else if (!strcmp(name, "ndash") || !strcmp(name, "mdash")) cp = '-';
        else if (!strcmp(name, "hellip")) cp = 0x2026;
        if (cp > 0) { addCodepoint(o, (uint32_t)cp); p = q + n + 1; continue; }
      }
      o.put('&');
      p++;
      continue;
    }
    if (c < 0x80) { addCodepoint(o, c); p++; continue; }
    // UTF-8 sequence
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : -1;
    if (extra < 0) { p++; continue; }               // stray continuation byte
    uint32_t cp = c & (0x3F >> extra);
    p++;
    for (int i = 0; i < extra; i++) {
      if ((*p & 0xC0) != 0x80) { cp = 0; break; }
      cp = (cp << 6) | (*p & 0x3F);
      p++;
    }
    if (cp) addCodepoint(o, cp);
  }
  while (o.len > 0 && out[o.len - 1] == ' ') o.len--;          // trailing spaces
  if (cap) out[o.len < cap ? o.len : cap - 1] = 0;
}

// ---------------- RSS ----------------
void RssParser::begin(int maxItems) {
  _max = maxItems < 1 ? 1 : (maxItems > MAX_ITEMS ? MAX_ITEMS : maxItems);
  _n = 0; _state = TEXT; _inItem = _inTitle = _nameDone = false;
  _tagLen = 0; _c1 = _c2 = 0; _rawLen = 0;
}

void RssParser::feed(char c) {
  if (_n >= _max) return;
  switch (_state) {
    case TEXT:
      if (c == '<') { _state = TAG; _tagLen = 0; _nameDone = false; }
      else if (_inTitle) addRaw(c);
      break;
    case TAG:
      if (c == '>') {
        _tag[_tagLen] = 0;
        endTag();
        _state = TEXT;
      } else if (!_nameDone) {
        if (isspace((unsigned char)c) || (c == '/' && _tagLen > 0)) _nameDone = true;
        else if (_tagLen < 15) {
          _tag[_tagLen++] = c;
          if (_tagLen == 8 && memcmp(_tag, "![CDATA[", 8) == 0) { _state = CDATA; _c1 = _c2 = 0; }
        }
      }
      break;
    case CDATA:
      if (c == '>' && _c1 == ']' && _c2 == ']') {
        if (_inTitle && _rawLen >= 2) _rawLen -= 2;           // the "]]" already stored
        _state = TEXT;
      } else if (_inTitle) {
        addRaw(c);
      }
      _c2 = _c1;
      _c1 = c;
      break;
  }
}

void RssParser::endTag() {
  if (!strcasecmp(_tag, "item") || !strcasecmp(_tag, "entry")) { _inItem = true; _inTitle = false; _rawLen = 0; }
  else if (!strcasecmp(_tag, "/item") || !strcasecmp(_tag, "/entry")) { _inItem = false; _inTitle = false; }
  else if (_inItem && !strcasecmp(_tag, "title")) { _inTitle = true; _rawLen = 0; }
  else if (_inItem && !strcasecmp(_tag, "/title")) { if (_inTitle) finishTitle(); _inTitle = false; }
}

void RssParser::finishTitle() {
  _raw[_rawLen] = 0;
  char clean[TITLE_LEN];
  tickerCleanText(_raw, clean, sizeof(clean));
  if (clean[0] && _n < _max) {
    strcpy(_t[_n], clean);
    _n++;
  }
  _rawLen = 0;
}

// ---------------- chunked bodies ----------------
bool Dechunker::feed(char c) {
  switch (_st) {
    case SIZE:
      if (c >= '0' && c <= '9') _size = _size * 16 + (c - '0');
      else if (c >= 'a' && c <= 'f') _size = _size * 16 + (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') _size = _size * 16 + (c - 'A' + 10);
      else if (c == ';') _st = EXT;
      else if (c == '\r') _st = LF1;
      else if (c == '\n') _st = _size ? DATA : END;
      return false;
    case EXT:
      if (c == '\r') _st = LF1;
      else if (c == '\n') _st = _size ? DATA : END;
      return false;
    case LF1:
      if (c == '\n') _st = _size ? DATA : END;
      return false;
    case DATA:
      if (--_size == 0) _st = CR;
      return true;
    case CR:
      if (c == '\r') _st = LF2;
      else if (c == '\n') { _st = SIZE; _size = 0; }
      return false;
    case LF2:
      _st = SIZE; _size = 0;
      return false;
    case END:
    default:
      return false;
  }
}

// ---------------- quotes ----------------
static void shownSymbol(const char *raw, char *out, size_t cap) {
  size_t n = 0;
  const char *p = raw;
  if (*p == '^') p++;
  for (; *p && n + 1 < cap; p++) out[n++] = (char)toupper((unsigned char)*p);
  out[n] = 0;
  if (n > 3 && !strcmp(out + n - 3, ".US")) out[n - 3] = 0;
}

// Splits one CSV line in place; returns the number of fields.
static int splitCsv(char *line, char **f, int maxF) {
  int n = 0;
  char *p = line;
  while (n < maxF) {
    f[n++] = p;
    char *c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  for (int i = 0; i < n; i++) {                      // trim spaces and quotes
    char *s = f[i];
    while (*s == ' ' || *s == '"') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '"' || e[-1] == '\r')) *--e = 0;
    f[i] = s;
  }
  return n;
}

int parseStooqCsv(const char *csv, Quote *out, int maxOut) {
  int nOut = 0, colSym = -1, colOpen = -1, colClose = -1, colPrev = -1;
  bool header = true;
  const char *p = csv;
  while (*p && nOut < maxOut) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    char line[200];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    p = nl ? nl + 1 : p + strlen(p);
    if (len < 3) continue;

    char *f[16];
    int nf = splitCsv(line, f, 16);
    if (header) {
      for (int i = 0; i < nf; i++) {
        if (!strcasecmp(f[i], "Symbol")) colSym = i;
        else if (!strcasecmp(f[i], "Open")) colOpen = i;
        else if (!strcasecmp(f[i], "Close")) colClose = i;
        else if (!strncasecmp(f[i], "Prev", 4)) colPrev = i;
      }
      if (colSym < 0 || colClose < 0 || (colOpen < 0 && colPrev < 0)) return 0;
      header = false;
      continue;
    }
    if (colSym >= nf || colClose >= nf) continue;
    char *endp;
    double close = strtod(f[colClose], &endp);
    if (endp == f[colClose] || close <= 0) continue;              // "N/D": no data
    double base = 0;
    if (colPrev >= 0 && colPrev < nf) base = strtod(f[colPrev], nullptr);
    if (base <= 0 && colOpen >= 0 && colOpen < nf) base = strtod(f[colOpen], nullptr);
    if (base <= 0) continue;
    Quote &q = out[nOut++];
    shownSymbol(f[colSym], q.sym, sizeof(q.sym));
    q.price = (float)close;
    q.pct = (float)((close - base) / base * 100.0);
  }
  return nOut;
}

// ---------------- JSON ----------------
static const char *findJsonKey(const char *json, const char *key) {
  char pat[32];
  int pl = snprintf(pat, sizeof(pat), "\"%s\":", key);
  if (pl <= 0 || pl >= (int)sizeof(pat)) return nullptr;
  const char *p = json;
  while ((p = strstr(p, pat)) != nullptr) {
    char before = (p == json) ? '{' : p[-1];
    if (before == '{' || before == ',' || before == ' ' || before == '\n') {
      p += pl;
      while (*p == ' ') p++;
      return p;
    }
    p += pl;
  }
  return nullptr;
}

bool jsonNumber(const char *json, const char *key, double &value) {
  const char *p = findJsonKey(json, key);
  if (!p) return false;
  char *endp;
  double v = strtod(p, &endp);
  if (endp == p) return false;
  value = v;
  return true;
}

bool jsonString(const char *json, const char *key, char *out, size_t cap) {
  const char *p = findJsonKey(json, key);
  if (!p || *p != '"') return false;
  p++;
  size_t n = 0;
  while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
  out[n] = 0;
  return true;
}

bool parseYahooChart(const char *json, Quote &q) {
  double price, prev;
  if (!jsonNumber(json, "regularMarketPrice", price)) return false;
  if (!jsonNumber(json, "chartPreviousClose", prev) && !jsonNumber(json, "previousClose", prev)) return false;
  if (prev <= 0) return false;
  char sym[12] = "";
  jsonString(json, "symbol", sym, sizeof(sym));
  shownSymbol(sym, q.sym, sizeof(q.sym));
  q.price = (float)price;
  q.pct = (float)((price - prev) / prev * 100.0);
  return true;
}

// ---------------- formatting ----------------
void fmtRate(char *out, size_t n, double v) {
  if (v >= 1000)      snprintf(out, n, "%.0f", v);
  else if (v >= 100)  snprintf(out, n, "%.1f", v);
  else if (v >= 10)   snprintf(out, n, "%.2f", v);
  else if (v >= 1)    snprintf(out, n, "%.3f", v);
  else                snprintf(out, n, "%.4f", v);
}

void fmtPct(char *out, size_t n, float pct) {
  if (fabsf(pct) >= 10) snprintf(out, n, "%+.0f%%", pct);
  else                  snprintf(out, n, "%+.1f%%", pct);
}
