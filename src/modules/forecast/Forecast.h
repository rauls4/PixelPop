#pragma once
// Forecast module: the daily weather forecast (up to 7 days) from Open-Meteo
// (free, no API key). Two looks: two days per screen (day, icon, high/low), or
// one day per screen with a big high, low, rain chance and icon.
// By default it uses the same location and units as the Weather module.

#include "../../core/Module.h"
#include "../../core/NetLock.h"

class ForecastModule : public Module {
 public:
  ForecastModule() : Module("forecast", "Forecast", "x_", true, 6) {}
  const char *version() const override { return "1.3.0"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override { return _valid && sub < pageCount(); }
  void drawPage(int sub) override;
  bool needsRedraw() override;
  int transitionOverride() override;
  void pageEntered(int sub) override { (void)sub; _scrollT0 = millis(); }

  const char *enabledLabel() override { return "Show the forecast in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  static const int MAX_DAYS = 7;

  // settings
  bool     _same = true;                          // use the Weather module's location + units
  bool     _celsius = false;
  float    _lat = 41.8745f, _lon = -87.6512f;     // Chicago
  uint8_t  _layout = 0;                           // 0 two days per screen, 1 one day per screen
  uint8_t  _days = 4;                             // 1..7
  uint32_t _colHigh = 0xFF9040, _colLow = 0x40A0FF;

  // data
  bool  _valid = false;
  bool  _dataCel = false;                         // units of the numbers below
  int   _nData = 0;                               // days received
  int   _wday[MAX_DAYS];                          // 0 = Sunday
  int   _code[MAX_DAYS];
  float _hi[MAX_DAYS], _lo[MAX_DAYS];
  int   _pop[MAX_DAYS];                           // chance of precipitation, %, -1 unknown

  // what the last fetch used (to notice when Weather's location changes)
  float _uLat = 0, _uLon = 0;
  bool  _uCel = false;

  // background task: downloads the forecast so the panel never waits on the network
  volatile bool _fetchNow = false;
  bool          _lastOk = false;
  bool          _everFetched = false;
  uint8_t       _fails = 0;                       // failed tries in a row (the first retries come quickly)
  unsigned long _lastFetch = 0, _lastCheck = 0;
  char          _status[112] = "";                 // why the last try failed, for the settings page
  unsigned long _scrollT0 = 0, _lastScrollStep = 0;
  void          setStatus(const char *s);

  void resolve(float &lat, float &lon, bool &cel);
  bool fetch();
  int  shownDays() const { return _nData < _days ? _nData : _days; }
  const char *tUnit() { return _dataCel ? "C" : "F"; }
  void drawTwo(int page);
  void drawOne(int day);
  void drawPortrait(int day);
};
