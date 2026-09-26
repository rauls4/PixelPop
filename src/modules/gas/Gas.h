#pragma once
// Gas Prices module: the AAA national average price of a gallon of gasoline, big, and how it has changed
// since yesterday, last week, last month and last year (scrolling along the bottom).
// The numbers come from the table on gasprices.aaa.com (no account needed); it is updated once a day.

#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "GasData.h"

class GasModule : public Module {
 public:
  GasModule() : Module("gas", "Gas Prices", "g_", true, 8) {}
  const char *version() const override { return "1.2.4"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  bool pageAvailable(int sub) override { (void)sub; return _valid; }
  void drawPage(int sub) override;
  bool needsRedraw() override;                       // the bottom line scrolls

  const char *enabledLabel() override { return "Show gas prices in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t  _grade = GAS_REGULAR;
  uint32_t _colPrice = 0xFFFFFF, _colUp = 0xFF5040, _colDown = 0x30E060, _colLabel = 0x00C8FF;

  // data
  bool  _valid = false;
  float _p[GAS_WHEN_COUNT] = {0, 0, 0, 0, 0};      // price now, yesterday, a week ago, a month ago, a year ago
  bool  _has[GAS_WHEN_COUNT] = {false, false, false, false, false};
  unsigned long _gotAt = 0;

  // scrolling
  unsigned long _scrollT0 = 0, _lastDraw = 0, _lastStep = 0;

  // background fetching
  volatile bool _fetchNow = false;
  bool          _lastOk = false, _everFetched = false;
  uint8_t       _fails = 0;
  unsigned long _lastFetch = 0;
  char          _status[112] = "";
  void setStatus(const char *s);
  bool fetch();
};
