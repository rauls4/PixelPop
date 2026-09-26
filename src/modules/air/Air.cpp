#include "Air.h"
#include <Wire.h>
#include <math.h>
#include <string.h>
#include <Fonts/TomThumb.h>            // tiny 3x5 font for labels
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/SmoothFont.h"
#include "../../core/I2cBus.h"
#include "../../core/Speaker.h"
#include "../../core/WebUI.h"

static const uint8_t SHTC3_ADDR = 0x70;
static const unsigned long SAMPLE_MS = 10UL * 1000;          // a reading every 10 seconds
static const unsigned long HIST_MS = 10UL * 60 * 1000;       // one history point every 10 minutes
static const unsigned long FRESH_MS = 90UL * 1000;           // readings older than this are not shown
static const int OFFSET_COUNT = 17;                          // 0.0 ... 8.0 degrees C in half degrees

static const uint32_t DEF_TEMP = 0xFF9A3C, DEF_HUM = 0x40C8FF, DEF_LABEL = 0x8A93A6;
static const uint32_t COL_GOOD = 0x30E060, COL_FAIR = 0xFFD200, COL_POOR = 0xFF5030;

// ---------- settings ----------
void AirModule::onLoad() {
  // Air Quality first used the flash prefix "a_", which the Flights function also uses. Carry the
  // settings over once to the prefix of its own.
  if (store.getU8("mig", 0) == 0) {
    Store old("a_");
    store.putBool("cel", old.getBool("cel", false));
    store.putU8("off", old.getU8("off", 0));
    store.putBool("pgB", old.getBool("pgB", true));
    store.putBool("pgD", old.getBool("pgD", true));
    store.putBool("pgT", old.getBool("pgT", true));
    store.putUInt("cTmp", old.getUInt("cTmp", DEF_TEMP));
    store.putUInt("cHum", old.getUInt("cHum", DEF_HUM));
    store.putUInt("cLbl", old.getUInt("cLbl", DEF_LABEL));
    store.putU8("mig", 1);
  }
  _celsius   = store.getBool("cel", false);
  _offsetIdx = store.getU8("off", 0);
  if (_offsetIdx >= OFFSET_COUNT) _offsetIdx = 0;
  _pgBig     = store.getBool("pgB", true);
  _pgDetails = store.getBool("pgD", true);
  _pgTrend   = store.getBool("pgT", true);
  _colTemp   = store.getUInt("cTmp", DEF_TEMP);
  _colHum    = store.getUInt("cHum", DEF_HUM);
  _colLabel  = store.getUInt("cLbl", DEF_LABEL);
}

String AirModule::onSettingsHtml() {
  String h;
  h += uiSection("Readings");
  static const char *const units[] = {"Fahrenheit (F)", "Celsius (C)"};
  h += uiSelect("cel", "Temperature unit", units, 2, _celsius ? 1 : 0);
  static char names[OFFSET_COUNT][16];
  static const char *ptrs[OFFSET_COUNT];
  for (int i = 0; i < OFFSET_COUNT; i++) {
    if (i == 0) snprintf(names[i], sizeof(names[i]), "No correction");
    else snprintf(names[i], sizeof(names[i]), "%.1f C lower", i * 0.5f);
    ptrs[i] = names[i];
  }
  h += uiSelect("off", "Correct a sensor that reads warm", ptrs, OFFSET_COUNT, _offsetIdx);
  h += "<p class='m'>The temperature sensor sits right next to the processor and the panel driver, so it usually "
       "reads a few degrees too warm. Let the board run for half an hour, compare with a thermometer you trust, and "
       "pick the difference here. The humidity is corrected to match (warmer air of the same moisture reads drier).</p>";

  h += uiSection("Pages");
  h += uiCheckbox("pgB", "Big temperature and humidity", _pgBig);
  h += uiCheckbox("pgD", "Details (dew point and comfort rating)", _pgDetails);
  h += uiCheckbox("pgT", "Trend graphs (the last several hours, appears after about 20 minutes)", _pgTrend);

  h += uiSection("Colors");
  h += uiColor("cTmp", "Temperature", _colTemp);
  h += uiColor("cHum", "Humidity", _colHum);
  h += uiColor("cLbl", "Labels", _colLabel);

  h += "<p class='m'>This board has an SHTC3 temperature and humidity sensor but no gas, particle or CO2 sensor, "
       "so the air readings are about comfort, not pollution.</p>";

  char st[80] = "";
  float rt = 0, rr = 0, t = 0, rh = 0;
  bool valid = false;
  if (_lock.take(100)) { strncpy(st, _status, sizeof(st) - 1); rt = _rawT; rr = _rawRh; t = _t; rh = _rh; valid = _valid; _lock.give(); }
  if (valid) {
    char buf[140];
    snprintf(buf, sizeof(buf), "Sensor says %.1f C, %.0f%%. Shown: %.1f C (%.1f F), %.0f%%.", rt, rr, t, envCtoF(t), rh);
    h += String("<p class='m'>") + htmlEscape(String(buf)) + "</p>";
  }
  if (st[0]) h += "<p class='m'>Sensor: " + htmlEscape(String(st)) + "</p>";
  return h;
}

void AirModule::onSave(WebServer &server) {
  _celsius   = uiReadLong(server, "cel", _celsius ? 1 : 0, 0, 1) == 1;
  _offsetIdx = (uint8_t)uiReadLong(server, "off", _offsetIdx, 0, OFFSET_COUNT - 1);
  _pgBig     = server.hasArg("pgB");
  _pgDetails = server.hasArg("pgD");
  _pgTrend   = server.hasArg("pgT");
  uint32_t c;
  if (uiReadColor(server, "cTmp", c)) _colTemp = c;
  if (uiReadColor(server, "cHum", c)) _colHum = c;
  if (uiReadColor(server, "cLbl", c)) _colLabel = c;
  store.putBool("cel", _celsius);
  store.putU8("off", _offsetIdx);
  store.putBool("pgB", _pgBig);
  store.putBool("pgD", _pgDetails);
  store.putBool("pgT", _pgTrend);
  store.putUInt("cTmp", _colTemp);
  store.putUInt("cHum", _colHum);
  store.putUInt("cLbl", _colLabel);
  _sampleNow = true;                                  // apply the correction right away
}

String AirModule::summary() {
  if (!_valid) return _status[0] ? String(_status) : String("Waiting for the first reading");
  EnvLevel lvl;
  const char *word = envComfort(_t, _rh, lvl);
  char buf[80];
  snprintf(buf, sizeof(buf), "%.1f %s, %.0f%% humidity - %s", show(_t), _celsius ? "C" : "F", _rh, word);
  return String(buf);
}

// ---------- the sensor ----------
void AirModule::begin() {
  if (_taskStarted) return;
  _taskStarted = true;
  _sda = I2C_PIN_SDA;
  _scl = I2C_PIN_SCL;
  if (xTaskCreatePinnedToCore(taskEntry, "air", 4096, this, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    _taskStarted = false;
    Serial.println("Air: could not start the sensor task");
  }
}

void AirModule::taskEntry(void *self) { ((AirModule *)self)->taskLoop(); }

void AirModule::taskLoop() {
  vTaskDelay(pdMS_TO_TICKS(3000));
  unsigned long last = 0;
  bool first = true;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!enabled()) continue;
    const bool force = _sampleNow;
    _sampleNow = false;
    if (first || force || millis() - last >= SAMPLE_MS) {
      sample();
      last = millis();
      first = false;
    }
  }
}

static bool writeCmd(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

// One measurement: wake, measure (temperature first, normal power), read 6 bytes, sleep.
bool AirModule::readShtc3(float &tempC, float &rh) {
  if (!writeCmd(0x3517)) return false;                // wake up
  vTaskDelay(pdMS_TO_TICKS(2));
  bool ok = writeCmd(0x7866);                         // measure, no clock stretching
  uint8_t b[6] = {0};
  if (ok) {
    vTaskDelay(pdMS_TO_TICKS(16));                    // takes about 12 ms
    ok = Wire.requestFrom((int)SHTC3_ADDR, 6) == 6;
    for (int i = 0; ok && i < 6; i++) b[i] = (uint8_t)Wire.read();
  }
  writeCmd(0xB098);                                   // back to sleep
  return ok && envDecodeShtc3(b, tempC, rh);
}

void AirModule::sample() {
  if (!i2cLock(1500)) return;                         // the speaker has the bus right now; try again soon
  const bool codecOpen = speakerCodecOpen();          // then the bus is already running
  float t = 0, rh = 0;
  bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok; attempt++) {
    if (!codecOpen) Wire.begin(_sda, _scl, 100000);
    ok = readShtc3(t, rh);
    if (!codecOpen) Wire.end();
    if (ok || _everOk || codecOpen) break;
    const int8_t s = _sda; _sda = _scl; _scl = s;     // never worked yet: try the two wires the other way round
  }
  i2cUnlock();
  if (ok) {
    _everOk = true;
    applyReading(t, rh);
  } else if (_lock.take(100)) {
    snprintf(_status, sizeof(_status), "the SHTC3 did not answer (I2C address 0x70)");
    _lock.give();
  }
}

void AirModule::applyReading(float rawT, float rawRh) {
  float t, rh;
  envCorrect(rawT, rawRh, _offsetIdx * 0.5f, t, rh);
  const float dew = envDewPointC(t, rh);
  if (!_lock.take(200)) return;
  _rawT = rawT; _rawRh = rawRh; _t = t; _rh = rh; _dew = dew;
  _okAt = millis();
  _valid = true;
  snprintf(_status, sizeof(_status), "working");
  if (_histAt == 0 || millis() - _histAt >= HIST_MS) { _hist.add(t, rh); _histAt = millis(); }
  _gen++;
  _lock.give();
}

// ---------- drawing ----------
int AirModule::kinds(Kind *out) {
  int n = 0;
  if (_pgBig) out[n++] = BIG;
  if (_pgDetails) out[n++] = DETAILS;
  if (_pgTrend) out[n++] = TREND;
  if (n == 0) out[n++] = BIG;                         // never leave the module with no page
  return n;
}

int AirModule::pageCount() {
  Kind k[3];
  return kinds(k);
}

bool AirModule::pageAvailable(int sub) {
  if (!_valid || millis() - _okAt > FRESH_MS) return false;
  Kind k[3];
  const int n = kinds(k);
  if (sub < 0 || sub >= n) return false;
  if (k[sub] == TREND && _hist.count() < 3) return false;
  return true;
}

bool AirModule::needsRedraw() { return _gen != _drawnGen; }

static void tiny(int x, int baseline, const char *s, uint16_t col) {
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(col);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(s);
  gDisplay->setFont(nullptr);
}
static int tinyW(const char *s) { return (int)strlen(s) * 4 - 1; }
static uint16_t levelColor(EnvLevel l) { return rgb565(l == ENV_GOOD ? COL_GOOD : l == ENV_FAIR ? COL_FAIR : COL_POOR); }

// A number in the middle of a half-width column, big if it is short, with a tiny unit after it.
// Smooth font when it fits the column; falls back to the blown-up bitmap font when it doesn't.
static void bigValue(int x0, int w, const char *num, const char *unit, uint16_t col) {
  const int unitW = tinyW(unit);
  gDisplay->setTextColor(col);
  SmoothFit fit = fitSmoothFont(num, w - unitW - 3, 13);
  if (fit.font) {
    const int total = (int)fit.w + 1 + unitW;
    const int x = x0 + (w - total) / 2, top = 10;
    gDisplay->setCursor(x - fit.xBearing, top - fit.top);
    gDisplay->print(num);
    gDisplay->antialiasText(x, top, fit.w, fit.h, col);
    gDisplay->setFont(nullptr);
    tiny(x + (int)fit.w + 1, top + (int)fit.h - 2, unit, col);
  } else {
    const int size = (strlen(num) <= 2) ? 2 : 1;
    const int nw = (int)strlen(num) * 6 * size - size;
    const int total = nw + 1 + unitW;
    const int x = x0 + (w - total) / 2;
    const int y = (size == 2) ? 6 : 10;
    gDisplay->setTextSize(size);
    gDisplay->setCursor(x, y);
    gDisplay->print(num);
    gDisplay->setTextSize(1);
    tiny(x + nw + 1, y + 5, unit, col);
  }
}

void AirModule::drawBig(float t, float rh) {
  const uint16_t lbl = rgb565(_colLabel);
  const int W = gDisplay->width(), H = gDisplay->height();
  char n1[8], n2[8];
  snprintf(n1, sizeof(n1), "%d", (int)lroundf(show(t)));
  snprintf(n2, sizeof(n2), "%d", (int)lroundf(rh));
  if (H > W) {
    tiny((W - tinyW("TEMP")) / 2, 5, "TEMP", lbl);
    bigValue(0, W, n1, _celsius ? "C" : "F", rgb565(_colTemp));
    tiny((W - tinyW("HUMID")) / 2, 29, "HUMID", lbl);
    gDisplay->setTextColor(rgb565(_colHum));
    SmoothFit fitH = fitSmoothFont(n2, W - 8, 13);
    if (fitH.font) {
      const int totalH = (int)fitH.w + 1 + tinyW("%");
      const int xh = (W - totalH) / 2, topH = 33;
      gDisplay->setCursor(xh - fitH.xBearing, topH - fitH.top);
      gDisplay->print(n2);
      gDisplay->antialiasText(xh, topH, fitH.w, fitH.h, rgb565(_colHum));
      gDisplay->setFont(nullptr);
      tiny(xh + (int)fitH.w + 1, topH + (int)fitH.h - 2, "%", rgb565(_colHum));
    } else {
      gDisplay->setTextSize(2);
      const int humW = (int)strlen(n2) * 12 - 2;
      gDisplay->setCursor((W - humW - 5) / 2, 34);
      gDisplay->print(n2);
      gDisplay->setTextSize(1);
      tiny((W + humW) / 2 - 1, 39, "%", rgb565(_colHum));
    }
    EnvLevel lvl;
    const char *word = envComfort(t, rh, lvl);
    gDisplay->setTextColor(levelColor(lvl));
    gDisplay->setCursor((W - (int)strlen(word) * 6 + 1) / 2, H - 9);
    gDisplay->print(word);
    return;
  }
  tiny(1, 5, "TEMP", lbl);
  tiny(W / 2 + 1, 5, "HUMID", lbl);
  bigValue(0, W / 2, n1, _celsius ? "C" : "F", rgb565(_colTemp));
  bigValue(W / 2, W - W / 2, n2, "%", rgb565(_colHum));
  EnvLevel lvl;
  const char *word = envComfort(t, rh, lvl);
  const int w = (int)strlen(word) * 6 - 1;
  gDisplay->setTextColor(levelColor(lvl));
  gDisplay->setCursor((W - w) / 2, H - 8);
  gDisplay->print(word);
}

void AirModule::drawDetails(float t, float rh, float dew) {
  const uint16_t lbl = rgb565(_colLabel);
  const int W = gDisplay->width(), H = gDisplay->height();
  const char *unit = _celsius ? "C" : "F";
  struct Row { const char *label; char text[16]; uint16_t col; };
  Row rows[4];
  rows[0].label = "TEMP";   snprintf(rows[0].text, 16, "%.1f%s", show(t), unit);  rows[0].col = rgb565(_colTemp);
  rows[1].label = "HUMID";  snprintf(rows[1].text, 16, "%.1f%%", rh);             rows[1].col = rgb565(_colHum);
  rows[2].label = "DEW PT"; snprintf(rows[2].text, 16, "%.1f%s", show(dew), unit); rows[2].col = rgb565(0xC8D0E0);
  EnvLevel lvl;
  rows[3].label = "FEELS";  snprintf(rows[3].text, 16, "%s", envComfort(t, rh, lvl)); rows[3].col = levelColor(lvl);
  if (H > W) {
    for (int i = 0; i < 4; i++) {
      const int y = i * 15;
      tiny((W - tinyW(rows[i].label)) / 2, y + 5, rows[i].label, lbl);
      const int w = (int)strlen(rows[i].text) * 6 - 1;
      gDisplay->setTextColor(rows[i].col);
      gDisplay->setCursor((W - w) / 2, y + 7);
      gDisplay->print(rows[i].text);
    }
    return;
  }
  for (int i = 0; i < 4; i++) {
    const int y = i * (H - 7) / 3;                    // four rows of 7 pixels spread over the height
    tiny(1, y + 6, rows[i].label, lbl);
    const int w = (int)strlen(rows[i].text) * 6 - 1;
    gDisplay->setTextColor(rows[i].col);
    gDisplay->setCursor(W - 1 - w, y);
    gDisplay->print(rows[i].text);
  }
}

// Two small graphs: temperature on top, humidity below, newest reading at the right.
void AirModule::drawTrend() {
  const bool portrait = gDisplay->height() > gDisplay->width();
  const int X0 = portrait ? 1 : 14, W = gDisplay->width() - X0 - (portrait ? 1 : 0), H = gDisplay->height();
  const int n = _hist.count() < W ? _hist.count() : W;
  const int first = _hist.count() - n;
  for (int g = 0; g < 2; g++) {
    const int h = (H - 2) / 2, top = g ? H - h : 0;
    const uint16_t col = rgb565(g ? _colHum : _colTemp);
    float v[EnvHistory::N];
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; i++) {
      v[i] = g ? _hist.rh(first + i) : show(_hist.temp(first + i));
      if (v[i] < lo) lo = v[i];
      if (v[i] > hi) hi = v[i];
    }
    const float minRange = g ? 10.0f : (_celsius ? 2.0f : 4.0f);
    if (hi - lo < minRange) { const float mid = (hi + lo) / 2; lo = mid - minRange / 2; hi = mid + minRange / 2; }
    char a[8];
    if (portrait) {
      tiny(1, top + 5, g ? "HUM" : "TEMP", col);
      const int graphTop = top + 7;
      const int graphH = h - 8;
      int px = 0, py = 0;
      for (int i = 0; i < n; i++) {
        const int x = X0 + W - n + i;
        const int y = graphTop + graphH - 1 - (int)lroundf((v[i] - lo) / (hi - lo) * (graphH - 1));
        if (i) gDisplay->drawLine(px, py, x, y, col);
        px = x; py = y;
      }
      gDisplay->fillRect(px - 1, py - 1, 2, 2, COL_WHITE);
      continue;
    }
    snprintf(a, sizeof(a), "%d", (int)lroundf(hi));
    tiny(0, top + 5, a, col);
    snprintf(a, sizeof(a), "%d", (int)lroundf(lo));
    tiny(0, top + h - 1, a, col);
    int px = 0, py = 0;
    for (int i = 0; i < n; i++) {
      const int x = X0 + W - n + i;
      const int y = top + h - 1 - (int)lroundf((v[i] - lo) / (hi - lo) * (h - 1));
      if (i) gDisplay->drawLine(px, py, x, y, col);
      px = x; py = y;
    }
    gDisplay->fillRect(px - 1, py - 1, 2, 2, COL_WHITE);       // the latest reading
  }
}

void AirModule::drawPage(int sub) {
  float t = 0, rh = 0, dew = 0;
  bool got = false;
  if (_lock.take(50)) { t = _t; rh = _rh; dew = _dew; _drawnGen = _gen; got = true; _lock.give(); }
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);
  Kind k[3];
  const int n = kinds(k);
  if (got && sub >= 0 && sub < n) {
    if (k[sub] == BIG) drawBig(t, rh);
    else if (k[sub] == DETAILS) drawDetails(t, rh, dew);
    else if (_lock.take(50)) { drawTrend(); _lock.give(); }
  }
  gDisplay->flipDMABuffer();
}
