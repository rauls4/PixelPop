#pragma once
// News ticker module: up to three rows of text that scroll sideways at the same time,
//   * stock moves (percent change for a watch list, biggest movers first),
//   * headlines (from an RSS news feed),
//   * currency conversion values (for example 1 USD = 0.92 EUR).
//
// Data comes from free services that need no account: Stooq or Yahoo Finance (stocks),
// an RSS feed such as NPR or BBC (headlines) and open.er-api.com (currency rates). It is
// fetched in a background task, so the display and the web pages never wait for it.

#include <vector>
#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "TickerData.h"

class TickerModule : public Module {
 public:
  TickerModule() : Module("ticker", "News Ticker", "t_", true, 20) {}
  const char *version() const override { return "1.0.3"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                     // every pixel of scrolling

  const char *enabledLabel() override { return "Show the ticker in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // one piece of a row: some text in one color
  struct Seg { String text; uint32_t color; };
  struct Row {
    std::vector<Seg> segs;
    int  width = 0;                                // pixels, all segments
    bool ready = false;
    void add(const String &t, uint32_t c) { segs.push_back({t, c}); width += (int)t.length() * 6; }
  };

  // ---- settings ----
  bool     _showStocks = true, _showNews = true, _showFx = true;
  bool     _labels = true;                         // small "MARKETS" / "NEWS" / "FX" tags at the start of each row
  uint8_t  _speed = 1;                             // 0 slow, 1 normal, 2 fast
  String   _symbols = "AAPL,MSFT,NVDA,TSLA,AMZN,GOOGL,META,SPY,QQQ,DIA";
  uint8_t  _stockSrc = 1;                          // 0 Stooq, 1 Yahoo Finance
  uint8_t  _thresh = 2;                            // index into THRESH: how big a move counts as significant
  uint8_t  _stockEvery = 1;                        // index into STOCK_MIN
  uint8_t  _newsSrc = 0;                           // index into the news sources (last = custom URL)
  String   _newsUrl = "";
  uint8_t  _newsCount = 10;
  uint8_t  _newsEvery = 1;                         // index into NEWS_MIN
  String   _base = "USD";
  String   _targets = "EUR,GBP,JPY,CAD,MXN,CNY";
  uint32_t _amount = 1;
  uint32_t _colUp = 0x30E060, _colDown = 0xFF4040, _colSym = 0xFFFFFF, _colNews = 0xFFD200,
           _colFx = 0x00C8FF, _colLabel = 0x8A93A6;

  // ---- data (written by the background task, read by the display) ----
  SimpleLock _lock;
  Row        _stocks, _news, _rates;
  int        _count[3] = {0, 0, 0};                // quotes / headlines / rates in each row
  bool       _failed[3] = {false, false, false};
  bool       _tried[3] = {false, false, false};
  unsigned long _lastTry[3] = {0, 0, 0};

  // ---- background task ----
  volatile bool _fetchNow = false;
  RssParser     _rss;
  void   fetchOne(int which);
  bool   fetchStocks();
  int    quotesFromStooq(const String *syms, int ns, Quote *q);
  int    quotesFromYahoo(const String *syms, int ns, Quote *q);
  bool   fetchNews();
  bool   fetchRates();
  void   publish(Row &dst, Row &src, int which, int count);

  // ---- display ----
  unsigned long _lastKey = 0;
  int  msPerPixel() const;
  void drawRow(const Row &r, int y, int width, long phase, int kind, bool onWhite = false);
  uint64_t rowPixels(int kind) const;          // pixels a row (0 stocks, 1 headlines, 2 currency) has travelled
  unsigned long stepKey() const;               // changes whenever some row has to move by a pixel
};
