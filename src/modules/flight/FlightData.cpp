#include "FlightData.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------- airports (airport reference points, rounded) ----------------
static const Airport AIRPORTS[] = {
  {"ORD", "Chicago O'Hare",         41.9786f,  -87.9048f},
  {"MDW", "Chicago Midway",         41.7868f,  -87.7522f},
  {"ATL", "Atlanta",                33.6407f,  -84.4277f},
  {"LAX", "Los Angeles",            33.9416f, -118.4085f},
  {"DFW", "Dallas/Fort Worth",      32.8998f,  -97.0403f},
  {"DEN", "Denver",                 39.8561f, -104.6737f},
  {"JFK", "New York JFK",           40.6413f,  -73.7781f},
  {"LGA", "New York LaGuardia",     40.7769f,  -73.8740f},
  {"EWR", "Newark",                 40.6895f,  -74.1745f},
  {"SFO", "San Francisco",          37.6213f, -122.3790f},
  {"SEA", "Seattle-Tacoma",         47.4502f, -122.3088f},
  {"MIA", "Miami",                  25.7959f,  -80.2870f},
  {"LAS", "Las Vegas",              36.0840f, -115.1537f},
  {"PHX", "Phoenix",                33.4342f, -112.0116f},
  {"IAH", "Houston Bush",           29.9902f,  -95.3368f},
  {"BOS", "Boston Logan",           42.3656f,  -71.0096f},
  {"MSP", "Minneapolis-St Paul",    44.8848f,  -93.2223f},
  {"DTW", "Detroit",                42.2162f,  -83.3554f},
  {"PHL", "Philadelphia",           39.8744f,  -75.2424f},
  {"CLT", "Charlotte",              35.2144f,  -80.9473f},
  {"YYZ", "Toronto Pearson",        43.6777f,  -79.6248f},
  {"MEX", "Mexico City",            19.4363f,  -99.0721f},
  {"LHR", "London Heathrow",        51.4700f,   -0.4543f},
  {"CDG", "Paris Charles de Gaulle",49.0097f,    2.5479f},
  {"AMS", "Amsterdam Schiphol",     52.3105f,    4.7683f},
  {"FRA", "Frankfurt",              50.0379f,    8.5622f},
  {"DXB", "Dubai",                  25.2532f,   55.3657f},
  {"HND", "Tokyo Haneda",           35.5494f,  139.7798f},
  {"SIN", "Singapore Changi",        1.3644f,  103.9915f},
  {"SYD", "Sydney",                -33.9399f,  151.1772f},
};

int airportCount() { return (int)(sizeof(AIRPORTS) / sizeof(AIRPORTS[0])); }
const Airport &airportAt(int i) {
  if (i < 0) i = 0;
  if (i >= airportCount()) i = airportCount() - 1;
  return AIRPORTS[i];
}

// ---------------- geometry ----------------
static const float DEG = 0.017453292f;

float geoDistanceNm(float lat1, float lon1, float lat2, float lon2) {
  float p1 = lat1 * DEG, p2 = lat2 * DEG;
  float dp = (lat2 - lat1) * DEG, dl = (lon2 - lon1) * DEG;
  float a = sinf(dp / 2) * sinf(dp / 2) + cosf(p1) * cosf(p2) * sinf(dl / 2) * sinf(dl / 2);
  if (a > 1) a = 1;
  float c = 2 * atan2f(sqrtf(a), sqrtf(1 - a));
  return c * 3440.065f;                       // Earth radius in nautical miles
}

float geoBearing(float lat1, float lon1, float lat2, float lon2) {
  float p1 = lat1 * DEG, p2 = lat2 * DEG, dl = (lon2 - lon1) * DEG;
  float y = sinf(dl) * cosf(p2);
  float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
  float b = atan2f(y, x) / DEG;
  if (b < 0) b += 360;
  return b;
}

const char *compass8(int d) {
  static const char *const N[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  d = ((d % 360) + 360) % 360;
  return N[((d + 22) / 45) % 8];
}

// ---------------- nearest-N list ----------------
void AircraftList::offer(const Aircraft &a) {
  int pos = n;
  while (pos > 0 && items[pos - 1].distNm > a.distNm) pos--;
  if (pos >= limit) return;                    // farther than everything we keep
  int last = (n < limit) ? n : limit - 1;      // slot that gets overwritten / appended
  for (int i = last; i > pos; i--) items[i] = items[i - 1];
  items[pos] = a;
  if (n < limit) n++;
}

// ---------------- JSON helpers (flat, top level of one aircraft) ----------------
// Find  "key":  where the key is really a key (preceded by { or ,).
static const char *skipWs(const char *p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
  return p;
}

static const char *findKey(const char *buf, int len, const char *key) {
  char pat[24];
  int pl = snprintf(pat, sizeof(pat), "\"%s\":", key);
  for (int i = 1; i + pl <= len; i++) {
    char p = buf[i - 1];
    if ((p == ',' || p == '{' || p == ' ' || p == '\n' || p == '\t' || p == '\r') && memcmp(buf + i, pat, pl) == 0)
      return skipWs(buf + i + pl);
  }
  return nullptr;
}

static bool getString(const char *buf, int len, const char *key, char *out, size_t n) {
  const char *p = findKey(buf, len, key);
  if (!p || *p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && p < buf + len) {
    if (*p == '\\' && p + 1 < buf + len) p++;
    if (i + 1 < n) out[i++] = *p;
    p++;
  }
  out[i] = 0;
  return true;
}

static bool getNumber(const char *buf, int len, const char *key, double &v) {
  const char *p = findKey(buf, len, key);
  if (!p) return false;
  if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.')) return false;   // "ground", null, ...
  char *end = nullptr;
  v = strtod(p, &end);
  return end != p;
}

static void trimInPlace(char *s) {
  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
  int i = 0;
  while (s[i] == ' ') i++;
  if (i) memmove(s, s + i, strlen(s + i) + 1);
}

static void upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

static bool isMajorUSPassengerCallsign(const char *call) {
  static const char *const PREFIXES[] = {
    "AAL", "DAL", "UAL", "SWA", "JBU", "ASA", "NKS", "FFT", "HAL", "AAY"
  };
  if (strlen(call) < 3) return false;
  for (const char *prefix : PREFIXES)
    if (strncmp(call, prefix, 3) == 0) return true;
  return false;
}

// ---------------- streaming parser ----------------
void AircraftStream::begin(AircraftList &list, float centerLat, float centerLon, float radiusNm, bool hideGround, bool majorOnly) {
  _list = &list; _list->clear();
  _clat = centerLat; _clon = centerLon; _radius = radiusNm; _hideGround = hideGround; _majorOnly = majorOnly;
  _phase = SEEK; memset(_win, 0, sizeof(_win)); _seen = 0;
  _inObj = _inStr = _esc = false; _depth = 0; _len = 0;
}

void AircraftStream::feed(char c) {
  if (_phase == DONE || !_list) return;

  if (_phase == SEEK) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;     // tolerate pretty-printed JSON
    // rolling window of the last 6 characters, looking for  "ac":[
    memmove(_win, _win + 1, 5);
    _win[5] = c;
    _win[6] = 0;
    if (memcmp(_win, "\"ac\":[", 6) == 0) _phase = ARRAY;
    return;
  }

  // ---- inside the array ----
  if (!_inObj) {
    if (c == '{') {
      _inObj = true; _inStr = false; _esc = false; _depth = 1; _len = 0;
      _buf[_len++] = '{';
    } else if (c == ']') {
      _phase = DONE;
    }
    return;
  }

  // ---- inside one aircraft object ----
  if (_inStr) {
    if (_esc) _esc = false;
    else if (c == '\\') _esc = true;
    else if (c == '"') _inStr = false;
  } else {
    if (c == '"') _inStr = true;
    else if (c == '{' || c == '[') { _depth++; }
    else if (c == '}' || c == ']') {
      _depth--;
      if (_depth == 0) { finishObject(); _inObj = false; return; }
      if (_depth >= 1) {                      // closed a nested value: nothing to keep
        return;
      }
    }
  }
  // keep only the top level of the object (depth 1); nested objects/arrays are skipped
  bool nestedOpener = (!_inStr && (c == '{' || c == '[') && _depth > 1);
  if (_depth == 1 && !nestedOpener && _len < BUF - 1) _buf[_len++] = c;
}

void AircraftStream::finishObject() {
  _seen++;
  _buf[_len] = 0;
  const char *b = _buf;
  int n = _len;

  double lat, lon;
  if (!getNumber(b, n, "lat", lat) || !getNumber(b, n, "lon", lon)) return;   // no position

  Aircraft a;
  memset(&a, 0, sizeof(a));
  a.gs = -1; a.track = -1;

  // altitude: a number, or the string "ground"
  const char *alt = findKey(b, n, "alt_baro");
  if (alt) {
    if (*alt == '"') a.onGround = (strncmp(alt, "\"ground\"", 8) == 0);
    else { double v; if (getNumber(b, n, "alt_baro", v)) { a.hasAlt = true; a.altFt = (int32_t)lround(v); } }
  }
  if (a.onGround && _hideGround) return;

  double v;
  if (getNumber(b, n, "gs", v))    a.gs = (int16_t)lround(v);
  if (getNumber(b, n, "track", v)) { int t = (int)lround(v) % 360; if (t < 0) t += 360; a.track = (int16_t)t; }
  if (getNumber(b, n, "baro_rate", v))      { a.vrate = (int16_t)lround(v); a.hasVrate = true; }
  else if (getNumber(b, n, "geom_rate", v)) { a.vrate = (int16_t)lround(v); a.hasVrate = true; }

  // emergency: the "emergency" text if present, otherwise the special transponder codes
  char em[12] = {0}, sq[6] = {0};
  getString(b, n, "emergency", em, sizeof(em));
  getString(b, n, "squawk", sq, sizeof(sq));
  if (!strcmp(em, "general"))        a.emerg = 1;
  else if (!strcmp(em, "lifeguard")) a.emerg = 2;
  else if (!strcmp(em, "minfuel"))   a.emerg = 3;
  else if (!strcmp(em, "nordo"))     a.emerg = 4;
  else if (!strcmp(em, "unlawful"))  a.emerg = 5;
  else if (!strcmp(em, "downed"))    a.emerg = 6;
  else if (!em[0] || !strcmp(em, "none")) {
    if (!strcmp(sq, "7700"))      a.emerg = 1;
    else if (!strcmp(sq, "7600")) a.emerg = 4;
    else if (!strcmp(sq, "7500")) a.emerg = 5;
  }

  getString(b, n, "t", a.type, sizeof(a.type));
  getString(b, n, "r", a.reg, sizeof(a.reg));
  upper(a.type);
  trimInPlace(a.reg);

  char flight[12] = {0};
  getString(b, n, "flight", flight, sizeof(flight));
  trimInPlace(flight);
  if (flight[0]) {
    strncpy(a.call, flight, sizeof(a.call) - 1);
  } else if (a.reg[0]) {
    strncpy(a.call, a.reg, sizeof(a.call) - 1);
  } else {
    char hex[10] = {0};
    getString(b, n, "hex", hex, sizeof(hex));
    upper(hex);
    strncpy(a.call, hex[0] ? hex : "?", sizeof(a.call) - 1);
  }
  upper(a.call);
  if (_majorOnly && !isMajorUSPassengerCallsign(a.call)) return;

  a.distNm = geoDistanceNm(_clat, _clon, (float)lat, (float)lon);
  if (a.distNm > _radius + 1.0f) return;
  a.bearing = (int16_t)lroundf(geoBearing(_clat, _clon, (float)lat, (float)lon)) % 360;
  _list->offer(a);
}

// ---------------- formatting ----------------
void fmtAltitude(char *out, size_t n, const Aircraft &a, int units) {
  if (a.onGround) { snprintf(out, n, "GND"); return; }
  if (!a.hasAlt)  { snprintf(out, n, "--"); return; }
  if (units == 2) snprintf(out, n, "%ldm", lround(a.altFt * 0.3048));
  else if (units == 0 && a.altFt >= 18000) snprintf(out, n, "FL%03ld", lround(a.altFt / 100.0));
  else            snprintf(out, n, "%ldft", (long)a.altFt);
}

const char *flightStatus(const Aircraft &a, int *kind) {
  int k = 0;
  const char *s = nullptr;
  static const char *const EM[] = {"", "MAYDAY", "MEDICAL", "LOW FUEL", "NO RADIO", "HIJACK", "DOWNED"};
  if (a.emerg >= 1 && a.emerg <= 6)  { s = EM[a.emerg]; k = 4; }
  else if (a.onGround) {
    if (a.gs >= 0) { s = (a.gs >= 3) ? "TAXI" : "STOPPED"; k = 3; }
  } else if (a.hasVrate) {
    if (a.vrate >= 300)       { s = "CLIMB";  k = 1; }
    else if (a.vrate <= -300) { s = "DESC";   k = 2; }
    else if (a.hasAlt && a.altFt >= 20000) { s = "CRUISE"; k = 3; }
    else                      { s = "LEVEL";  k = 3; }
  }
  if (kind) *kind = k;
  return s;
}

void fmtSpeed(char *out, size_t n, const Aircraft &a, int units) {
  if (a.gs < 0) { snprintf(out, n, "--"); return; }
  if (units == 2)      snprintf(out, n, "%ldkmh", lround(a.gs * 1.852));
  else if (units == 1) snprintf(out, n, "%ldmph", lround(a.gs * 1.15078));
  else                 snprintf(out, n, "%dkt", (int)a.gs);
}

void fmtDistance(char *out, size_t n, const Aircraft &a, int units, bool withUnit) {
  double d = a.distNm;
  const char *u = "nm";
  if (units == 1) { d *= 1.15078; u = "mi"; }
  else if (units == 2) { d *= 1.852; u = "km"; }
  if (d < 10) snprintf(out, n, withUnit ? "%.1f%s" : "%.1f", d, u);
  else        snprintf(out, n, withUnit ? "%ld%s" : "%ld", lround(d), u);
}

void fmtAltShort(char *out, size_t n, const Aircraft &a, int units) {
  if (a.onGround) { snprintf(out, n, "GND"); return; }
  if (!a.hasAlt)  { snprintf(out, n, "--"); return; }
  if (units == 2) {
    long m = lround(a.altFt * 0.3048);
    if (m < 1000) snprintf(out, n, "%ldm", m);
    else if (m < 10000) snprintf(out, n, "%.1fkm", m / 1000.0);
    else snprintf(out, n, "%ldkm", lround(m / 1000.0));
    return;
  }
  if (units == 0 && a.altFt >= 18000) { snprintf(out, n, "FL%03ld", lround(a.altFt / 100.0)); return; }
  if (a.altFt < 1000) snprintf(out, n, "%ldft", (long)a.altFt);
  else snprintf(out, n, "%.1fk", a.altFt / 1000.0);
}
