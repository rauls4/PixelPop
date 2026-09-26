#pragma once
// News ticker data helpers with no hardware in them (so they can be tested on a PC):
//   * turning headlines (UTF-8, XML entities) into plain text the panel font can show,
//   * a streaming RSS/Atom reader that keeps only the first few headlines,
//   * readers for stock quotes (Stooq CSV, Yahoo chart JSON) and currency rates (JSON),
//   * number formatting.

#include <stdint.h>
#include <stddef.h>

// ---- text ----
// Converts UTF-8 and &entities; to ASCII the panel font can show: curly quotes become
// straight ones, accents are dropped from letters, dashes become "-", and so on.
void tickerCleanText(const char *in, char *out, size_t cap);

// ---- RSS / Atom headlines ----
// Feed it the reply one character at a time. It records the <title> of each <item>
// (or Atom <entry>), and ignores everything else, so the size of the feed does not matter.
class RssParser {
 public:
  static const int MAX_ITEMS = 15;
  static const int TITLE_LEN = 110;
  void        begin(int maxItems);
  void        feed(char c);
  bool        done() const { return _n >= _max; }
  int         count() const { return _n; }
  const char *title(int i) const { return _t[i]; }
 private:
  enum State { TEXT, TAG, CDATA };
  static const int RAW_LEN = 360;
  State _state = TEXT;
  int   _max = 10, _n = 0;
  bool  _inItem = false, _inTitle = false, _nameDone = false;
  char  _tag[16];
  int   _tagLen = 0;
  char  _c1 = 0, _c2 = 0;
  char  _raw[RAW_LEN];
  int   _rawLen = 0;
  char  _t[MAX_ITEMS][TITLE_LEN];
  void endTag();
  void addRaw(char c) { if (_rawLen < RAW_LEN - 1) _raw[_rawLen++] = c; }
  void finishTitle();
};

// Removes the "chunked" framing of an HTTP body (only needed if a server chunks anyway).
class Dechunker {
 public:
  void begin() { _st = SIZE; _size = 0; }
  bool feed(char c);                      // true if c is part of the body
 private:
  enum St { SIZE, EXT, LF1, DATA, CR, LF2, END };
  St _st = SIZE;
  unsigned long _size = 0;
};

// ---- stock quotes ----
struct Quote {
  char  sym[12];        // shown symbol, e.g. "AAPL"
  float price;
  float pct;            // change in percent
};

// Stooq "q/l" CSV (header row + one row per symbol). Change is measured from the day's open.
int  parseStooqCsv(const char *csv, Quote *out, int maxOut);
// Yahoo chart JSON for one symbol (change from the previous close).
bool parseYahooChart(const char *json, Quote &q);

// ---- currency ----
// Finds  "KEY": number  in a JSON text (KEY is a currency code, or any key).
bool jsonNumber(const char *json, const char *key, double &value);
bool jsonString(const char *json, const char *key, char *out, size_t cap);
void fmtRate(char *out, size_t n, double v);        // 4 or 5 significant digits
void fmtPct(char *out, size_t n, float pct);        // "+2.3%"
