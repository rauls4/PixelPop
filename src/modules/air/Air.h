#pragma once
// Air Quality module: shows the room's temperature and humidity from the board's own SHTC3
// sensor (I2C, GPIO 47/48, address 0x70), plus the dew point, a plain-word comfort rating and
// a trend graph of the last several hours.
//
// This board has no gas, particle or CO2 sensor, so "air quality" here means the comfort of the
// air: temperature and humidity. The sensor sits close to the processor and reads a little warm;
// the settings page has a correction for that.

#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "EnvData.h"

class AirModule : public Module {
 public:
  AirModule() : Module("air", "Air Quality", "e_", true, 8) {}
  const char *version() const override { return "1.1.1"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void refreshNow() override { _sampleNow = true; }

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Show air readings in the rotation"; }
  String summary() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  enum Kind : uint8_t { BIG, DETAILS, TREND };

  // ---- settings ----
  bool     _celsius = false;
  uint8_t  _offsetIdx = 0;                         // degrees C to subtract, in half degrees (0 = no correction)
  bool     _pgBig = true, _pgDetails = true, _pgTrend = true;
  uint32_t _colTemp = 0xFF9A3C, _colHum = 0x40C8FF, _colLabel = 0x8A93A6;

  // ---- readings (written by the sensor task, read by the display) ----
  SimpleLock _lock;
  volatile bool _valid = false;
  float  _rawT = 0, _rawRh = 0;                    // straight from the sensor
  float  _t = 0, _rh = 0, _dew = 0;                // corrected
  unsigned long _okAt = 0;                         // millis of the last good reading
  unsigned long _histAt = 0;
  volatile unsigned _gen = 0;                      // bumps with every reading
  unsigned _drawnGen = 0;
  char   _status[80] = "";
  EnvHistory _hist;

  // ---- sensor task ----
  volatile bool _sampleNow = false;
  volatile bool _taskStarted = false;
  int8_t _sda, _scl;
  bool   _everOk = false;
  static void taskEntry(void *self);
  void   taskLoop();
  void   sample();
  bool   readShtc3(float &tempC, float &rh);
  void   applyReading(float rawT, float rawRh);

  // ---- display ----
  int  kinds(Kind *out);                            // the pages that are switched on
  void drawBig(float t, float rh);
  void drawDetails(float t, float rh, float dew);
  void drawTrend();
  float  show(float c) const { return _celsius ? c : envCtoF(c); }     // in the chosen unit
};
