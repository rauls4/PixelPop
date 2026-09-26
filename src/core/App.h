#pragma once
// The application core: owns the list of modules, rotates through their pages,
// handles the BOOT button and the connection watchdog.

#include <Arduino.h>
#include "Module.h"
#include "Orientation.h"

#define MAX_MODULES 24

class App {
 public:
  void add(Module *m);            // register a module (call before begin)
  void begin();
  void loop();

  // ---- module access (used by the web UI) ----
  int     count() const { return _n; }
  Module *module(int i) { return (i >= 0 && i < _n) ? _mods[i] : nullptr; }
  Module *find(const char *id);

  // ---- order of the modules (the order of pages on the panel and of the site's menu) ----
  bool    setOrder(const String &csv);      // "clock,weather,..." from the web page; saved
  String  orderCsv() const;

  // ---- general settings ----
  uint8_t brightness() const { return _brightness; }
  uint8_t tzIndex() const { return _tz; }
  uint8_t padding() const { return _pad; }        // unused pixels around the whole screen
  uint8_t transition() const { return _trStyle; }   // effect between pages (0 = none)
  uint8_t transitionSpeed() const { return _trSpeed; }   // 0 fast, 1 normal, 2 slow
  uint8_t colorOrder() const { return _colorOrder; }     // panel red/green/blue wiring fix (0 = normal)
  uint8_t rotation() const { return _rotation; }
  bool    use24Hour() const { return _use24Hour; }
  bool    portrait() const { return (_rotation & 1) != 0; }
  bool    autoOrientation() const { return _autoOrientation; }
  bool    imuAvailable() const;
  uint8_t imuAddress() const;
  OrientationSample imuSample() const;
  void    setGeneral(uint8_t brightness, uint8_t tz, uint8_t padding, uint8_t transition, uint8_t transitionSpeed, uint8_t colorOrder, bool use24Hour);
  void    setRotation(uint8_t rotation);
  void    setAutoOrientation(bool on);

  // ---- color calibration (Advanced > Color Lab: live on the panel while tuning) ----
  uint8_t gainR() const { return _gainR; }
  uint8_t gainG() const { return _gainG; }
  uint8_t gainB() const { return _gainB; }
  bool    calibrating() const { return _calMode; }
  void    calStart();                                                                 // enter: pauses the normal rotation
  void    calPreview(uint8_t order, uint8_t gainR, uint8_t gainG, uint8_t gainB, int swatch);  // live only, not saved
  void    calSave(uint8_t order, uint8_t gainR, uint8_t gainG, uint8_t gainB);         // persists to flash
  void    calExit();                                                                  // leave: resume the normal rotation

  // ---- actions ----
  void requestRedraw() { _needRedraw = true; }
  void refreshAll();              // ask every module to re-fetch its data
  void resetWifi();               // restart into Wi-Fi setup mode
  void showIp();                  // show Wi-Fi and network details
  void showAccessCode(uint32_t code, unsigned long ms = 30000);
  void showMessage(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr, unsigned long ms = 5000);   // up to three lines, held for ms
  void showFirmwareProgress(uint8_t percent);    // held while firmware is being installed
  // Settings-site hover preview: shows one module's page full-panel, live, in place of the
  // normal rotation. Call again (e.g. every ~500ms) while the mouse stays over that module's row
  // to keep it up; if calls stop, it auto-expires on its own shortly after (~1.2s), so a closed
  // tab or a dropped request can't leave the board stuck showing a preview.
  void previewModule(const char *id);

 private:
  Module *_mods[MAX_MODULES];
  int     _n = 0;

  uint8_t _brightness = 100;
  uint8_t _tz = 3;
  uint8_t _pad = 1;
  uint8_t _trStyle = 3;                 // default: slide left
  uint8_t _trSpeed = 1;
  uint8_t _colorOrder = 1;              // default: swap green and blue (this panel shows blue as green)
  bool    _use24Hour = false;
  uint8_t _rotation = 0;
  bool    _autoOrientation = true;
  uint8_t _gainR = 255, _gainG = 255, _gainB = 255;    // per-channel intensity balance, saved with _colorOrder
  bool          _calMode = false;             // Color Lab is live on the panel; the normal rotation is paused
  unsigned long _calTouch = 0;                // last time the Color Lab page touched the preview (for the idle timeout)

  int           _slot = 0;              // current page slot (module + sub-page)
  unsigned long _slotSince = 0;
  bool          _needRedraw = true;
  bool          _reenter = false;               // the page on screen should start over (after sleep or a notice)
  bool          _wasShowingIp = false;
  bool          _showingIpScreen = false;
  bool          _overlayOn = false;             // a module has taken over the whole panel (urgent notice)
  char          _previewId[24] = "";            // settings-site hover preview: module id, or empty
  unsigned long _previewUntil = 0;              // auto-expires shortly after the last previewModule() call
  bool          _previewActive = false;         // true while a preview is actually on screen (vs. just requested)
  bool          _showingMsg = false;
  bool          _spinning = false;            // the message screen is the spinner
  unsigned long _spinAt = 0;
  unsigned long _ipUntil = 0;
  unsigned long _ipShownAt = 0;
  unsigned long _lastIpFrame = 0;
  unsigned long _fastUntil = 0;         // the current page is animating: loop without the long pause
  unsigned long _buttonDownAt = 0;
  unsigned long _lastWifiRetry = 0;

  void applyOrder(const String &csv, bool keepScreen);
  int  slotCount();
  bool slotInfo(int idx, Module *&m, int &sub);
  bool slotAvailable(int idx);
  void applyRotation(uint8_t rotation);
  void handleButton(unsigned long now);
  void drawIpScreen();
};

extern App app;
