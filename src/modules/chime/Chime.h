#pragma once
// Chime module: plays Westminster, cuckoo-clock, or grandfather-clock sounds
// through the speaker. It has no page on the panel; it only listens to the clock.
//
//   Every minute      : a short phrase each minute, the proper quarter chimes at
//                       :15 / :30 / :45 and the full hour chime with strikes at :00
//   Quarter hours     : only :15 / :30 / :45 / :00
//   Hourly            : only :00
// Chimes only play during the selected daily time window.

#include "../../core/Module.h"
#include "Westminster.h"
#include "../../core/AudioOut.h"

class ChimeModule : public Module {
 public:
  ChimeModule() : Module("chime", "Chimes", "h_", true, 6) {}
  const char *version() const override { return "1.3.1"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void update() override;
  void registerRoutes(WebServer &server) override;

  bool hasPages() override { return false; }
  const char *enabledLabel() override { return "Play the chimes"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  uint8_t _style = 0;            // 0 Westminster, 1 Cuckoo, 2 Grandfather
  uint8_t _mode = 0;             // 0 every minute, 1 quarter hours, 2 hourly
  uint8_t _volume = 60;          // 1..100
  uint32_t _hours = 0xFFFFFF;    // bit n set = chimes allowed during hour n (0 = midnight hour). 0 = never
  uint8_t _startHour = 0;        // inclusive daily window start, 0..23
  uint8_t _endHour = 24;         // exclusive daily window end, 1..24
  uint8_t _output = 0;           // 0 onboard speaker (ES8311), 1 external I2S amplifier
  uint8_t _pinBclk = 4, _pinWs = 5, _pinDout = 6;   // external amplifier pins

  // runtime
  int   _lastMinute = -1;
  unsigned long _probeAt = 0;
  volatile bool _busy = false;
  char  _status[112] = "Not started yet";
  BellSynth _synth;
  AudioOut  _audio;
  AudioConfig _cfg;

  // playback request (set by startPlay, consumed by the playback task)
  ChimeStyle _playStyle = STYLE_WESTMINSTER;
  ChimeKind _kind = CHIME_TICK;
  int       _hour12 = 12;
  float     _levelMul = 1.0f;
  bool      _selfTest = false;

  bool hourAllowed(int hour) { return (_hours >> hour) & 1; }
  String hoursText();
  bool decide(const struct tm &t, ChimeKind &kind, float &level);
  void startPlay(ChimeKind kind, int hour12, float level);
  void startProbe();
  void playTask();
  static void taskEntry(void *arg);
  AudioConfig makeConfig();
};
