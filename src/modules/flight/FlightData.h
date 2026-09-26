#pragma once
// Flight data helpers with no hardware in them (so they can be tested on a PC):
//   * the list of major airports the tracker can be centered on,
//   * distance / bearing math,
//   * a streaming parser for the adsb.lol / readsb JSON ("ac" array) that keeps
//     only the aircraft nearest to the center, however large the reply is,
//   * unit formatting.

#include <stdint.h>
#include <stddef.h>

struct Airport {
  const char *code;      // IATA code
  const char *name;
  float lat, lon;
};

int            airportCount();
const Airport &airportAt(int i);

// ---- one aircraft ----
struct Aircraft {
  char    call[9];       // callsign (trimmed); falls back to registration, then the ICAO hex
  char    type[5];       // ICAO type code such as "B738", "" if unknown
  char    reg[9];        // registration such as "N123UA", "" if unknown
  bool    onGround;
  bool    hasAlt;
  int32_t altFt;         // barometric altitude, feet
  int16_t gs;            // ground speed, knots, -1 unknown
  int16_t track;         // direction of travel 0..359, -1 unknown
  int16_t vrate;         // climb (+) / descent (-), feet per minute, 0 if unknown
  bool    hasVrate;      // the climb/descent rate was reported
  uint8_t emerg;         // 0 none, 1 general emergency (7700), 2 medical, 3 low fuel, 4 radio failure (7600), 5 hijack (7500), 6 downed
  float   distNm;        // from the center
  int16_t bearing;       // from the center to the aircraft, degrees
  uint32_t firstSeen;    // millis() when it first appeared in the list, 0 = already there when tracking began
};

// Great-circle distance (nautical miles) and initial bearing (degrees).
float geoDistanceNm(float lat1, float lon1, float lat2, float lon2);
float geoBearing(float lat1, float lon1, float lat2, float lon2);
const char *compass8(int degrees);          // "N", "NE", ...

// ---- keeps the N nearest aircraft, sorted nearest first ----
class AircraftList {
 public:
  static const int CAP = 12;
  void  clear() { n = 0; }
  void  setLimit(int l) { limit = l < 1 ? 1 : (l > CAP ? CAP : l); }
  void  offer(const Aircraft &a);
  int   count() const { return n; }
  const Aircraft &at(int i) const { return items[i]; }
  Aircraft       &at(int i) { return items[i]; }
 private:
  Aircraft items[CAP];
  int n = 0, limit = 8;
};

// ---- streaming parser ----
// Feed it the reply one character at a time. For every aircraft that has a
// position, and is not filtered out, it is added to `list`.
class AircraftStream {
 public:
  AircraftStream() {}
  // Start a new reply. `list` receives the aircraft (it is cleared here).
  void begin(AircraftList &list, float centerLat, float centerLon, float radiusNm, bool hideGround, bool majorOnly);
  void feed(char c);
  bool done() const { return _phase == DONE; }
  bool sawArray() const { return _phase != SEEK; }
  int  seen() const { return _seen; }           // aircraft objects in the reply
 private:
  enum Phase { SEEK, ARRAY, DONE };
  static const int BUF = 1024;

  AircraftList *_list = nullptr;
  float _clat = 0, _clon = 0, _radius = 0;
  bool  _hideGround = true;
  bool  _majorOnly = true;
  Phase _phase = SEEK;

  char  _win[7] = {0};          // last characters, to find  "ac":[
  int   _seen = 0;
  // current object
  bool  _inObj = false, _inStr = false, _esc = false;
  int   _depth = 0;
  int   _len = 0;
  char  _buf[BUF];

  void finishObject();
};

// ---- text for the panel ----
// units: 0 aviation (ft / FL, kt, nm), 1 US (ft, mph, mi), 2 metric (m, km/h, km)
void fmtAltitude(char *out, size_t n, const Aircraft &a, int units);   // "12500ft", "FL350", "GND"
void fmtAltShort(char *out, size_t n, const Aircraft &a, int units);   // "12.5k", "FL350", "3.8km" (5 characters at most)
// Flight status shown beside the altitude, or nullptr when nothing is known.
// kind: 1 climbing, 2 descending, 3 neutral, 4 emergency.
const char *flightStatus(const Aircraft &a, int *kind);
void fmtSpeed(char *out, size_t n, const Aircraft &a, int units);      // "450kt"
void fmtDistance(char *out, size_t n, const Aircraft &a, int units, bool withUnit);   // "12nm"
