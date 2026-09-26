#pragma once
// Weather module: current conditions from Open-Meteo (free, no API key).
// One screen: the big temperature and the weather picture, with the condition, feels-like, humidity
// and wind scrolling along the bottom.

#include "../../core/Module.h"
#include "../../core/NetLock.h"

class WeatherModule : public Module {
 public:
  WeatherModule() : Module("weather", "Weather", "w_", true, 10) {}
  const char *version() const override { return "1.6.11"; }     // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }

  // where the weather is for (other modules, like In Season, use it to know the region)
  float  latitude() const { return _lat; }
  float  longitude() const { return _lon; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override { return 1; }
  bool pageAvailable(int sub) override { (void)sub; return _valid; }
  void drawPage(int sub) override;
  bool needsRedraw() override;                      // the bottom line scrolls

  const char *enabledLabel() override { return "Show weather in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  bool     _celsius = false;
  float    _lat = 41.8745f, _lon = -87.6512f;     // Chicago
  String   _city = "Chicago";                     // shown at the start of the scrolling line
  uint32_t _colTemp = 0xFFFFFF, _colCond = 0xFFD200, _colDetail = 0xFFFFFF, _colAccent = 0x00C8FF;

  // data
  bool  _valid = false;
  float _temp = 0, _feels = 0, _humidity = 0, _wind = 0;
  int   _code = 0;
  bool  _isDay = true;

  // scrolling line
  unsigned long _scrollT0 = 0, _lastDraw = 0, _lastFrame = 0;

  // ---- background fetching (the panel keeps animating while the download runs) ----
  volatile bool _fetchNow = false;
  bool          _lastOk = false;
  bool          _everFetched = false;
  uint8_t       _fails = 0;                 // failed tries in a row (the first retries come quickly)
  unsigned long _lastFetch = 0;
  char          _status[112] = "";           // why the last try failed, for the settings page
  void          setStatus(const char *s);
  bool fetch();
  const char *tUnit() { return _celsius ? "C" : "F"; }
  const char *wUnit() { return _celsius ? "kmh" : "mph"; }
  void drawIcon();
  void drawPortrait(unsigned long now);
};
