#pragma once
// Urgent notice: one message that takes over the whole panel with flashing alert triangles and,
// if you want, sounds a two-tone siren through the speaker. It fires ONCE, only when you press
// "Send" on its web page, stays for the time you set (or until you tap the BOOT button or press
// Stop), and then everything goes back to normal. Nothing repeats and nothing is scheduled.

#include "../../core/Module.h"
#include "Siren.h"

class UrgentModule : public Module {
 public:
  UrgentModule() : Module("urgent", "Urgent Notice", "u_", true, 10) {}
  const char *version() const override { return "1.0.0"; }      // bump when this module changes (see CHANGELOG.md)

  void update() override;
  void registerRoutes(WebServer &server) override;

  bool hasPages() override { return false; }        // never part of the rotation
  bool takesScreen() override { return _active; }
  void drawPage(int sub) override;
  bool needsRedraw() override;

  const char *enabledLabel() override { return "Allow urgent notices"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // settings
  String  _msg = "TEST ALERT";     // the last message sent (shown again in the form)
  uint16_t _secs = 30;             // how long it stays up
  uint8_t _volume = 70;            // siren volume 1..100
  bool    _sirenOn = true;

  // the notice on the panel
  volatile bool _active = false;
  bool          _preview = false;
  bool          _siren = false;
  String        _shown;            // the text on the panel (upper case)
  unsigned long _start = 0, _end = 0, _lastFrame = 0;
  uint32_t      _sent = 0;         // how many were sent since power-up

  // siren
  volatile bool _sirenBusy = false;
  char          _sirenStatus[96] = "";
  SirenSynth    _synth;

  bool trigger(const String &msg, bool preview);
  void finish();
  void sirenTask();
  static void taskEntry(void *arg);
};
