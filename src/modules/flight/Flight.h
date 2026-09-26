#pragma once
// Flight tracker module: shows the aircraft currently flying around a place you
// choose (a major airport, or any latitude/longitude), one aircraft per screen,
// nearest first: callsign, aircraft type, altitude, speed, distance and an arrow
// for the direction it is heading.
//
// Data: adsb.lol, a free community ADS-B network (no account or key). The list is
// fetched in a background task, so the display and web pages never wait for it.

#include "../../core/Module.h"
#include "FlightData.h"

class FlightModule : public Module {
 public:
  FlightModule() : Module("flights", "Flights", "a_", true, 5) {}
  const char *version() const override { return "1.1.6"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                 // true while the status text is scrolling

  const char *enabledLabel() override { return "Show flights in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t  _apt = 0;                  // 0 = custom coordinates, else airport index + 1
  float    _lat = 41.8745f, _lon = -87.6512f;    // custom center (Chicago)
  uint8_t  _radius = 25;              // nautical miles
  uint8_t  _maxShown = 8;             // aircraft per cycle
  bool     _hideGround = true;
  bool     _majorOnly = true;        // U.S. major passenger airlines only
  bool     _showEmpty = false;        // show a "no flights" page when nothing is in range
  uint8_t  _units = 0;                // 0 aviation, 1 US, 2 metric
  uint8_t  _poll = 30;                // seconds between updates
  bool     _showNew = true;           // red corner tag on newly listed flights
  bool     _showStatus = true;        // climbing / descending / ... beside the altitude
  bool     _showLogos = true;         // airline logo at the top left
  uint8_t  _logoBright = 70;          // logo brightness, percent (the pictures have white backgrounds)
  uint32_t _colCall = 0xFFD200, _colData = 0xFFFFFF, _colAccent = 0x00C8FF;

  // data: two copies so the background task can fill one while the display reads the other
  AircraftList    _snap[2];
  volatile uint8_t _cur = 0;
  volatile bool   _valid = false;
  volatile unsigned long _goodAt = 0;
  volatile int    _lastStatus = 0;    // last HTTP status / 0 = none, -1 = no Wi-Fi/parse problem
  uint8_t         _snapUnits = 0;
  int             _snapRadius = 25;
  char            _snapArea[8] = "";  // airport code or ""

  // background work
  AircraftList   _work;
  AircraftStream _stream;
  volatile bool  _fetchNow = false;
  bool           _lastOk = false;
  bool           _everFetched = false;
  unsigned long  _lastFetch = 0;

  // which flights have been in the list, and since when (used for the "new" corner tag)
  struct Seen { char call[9]; unsigned long first, last; };
  static const int SEEN_MAX = 40;
  Seen           _seen[SEEN_MAX];
  int            _nSeen = 0;
  bool           _seenBaseline = false;      // false until the first list has been recorded
  char           _seenKey[32] = "";          // where we are looking; a change starts over
  void stampSeen(AircraftList &l, unsigned long now, const char *key);
  bool isFresh(const Aircraft &a) const;

  // status text scrolling (only used when the text is wider than its space)
  bool           _scrolling = false;
  unsigned long  _scrollT0 = 0, _lastDraw = 0, _lastStep = 0;
  unsigned long  _lastTypePhase = ~0UL;
  int            _lastSub = -1;

  void drawBottomBar(const char *sp, const char *di);
  void drawStatus(const Aircraft &a, int xs, int xe);
  void drawTypeRow(int x, int y, int right, const char *text);
  void drawCallsign(int x, int y, int xe, const char *s, uint16_t col);   // bold, 1px between characters; scrolls if it doesn't fit

  bool   fetch();
  bool   fetchInner();
  void   center(float &lat, float &lon, char *label, size_t n);

  void drawAircraft(const Aircraft &a, int idx, int total);
  void drawWithLogo(const Aircraft &a, const char *code, int idx, int total);
  void drawPortrait(const Aircraft &a, const char *logoCode);
  void drawEmpty();
};
