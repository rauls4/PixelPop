#pragma once
// Firmware update from the internet: the board downloads one precompiled firmware file (the
// .ino.bin that Arduino's "Export Compiled Binary" makes) from a web address and installs it into
// the spare program area by itself. No cable, no IDE, no terminal.
//
// The download runs in its own background task so the panel keeps animating and shows the
// progress. The Advanced page (Advanced.cpp) asks for the code shown on the panel before it
// starts one.

#include <Arduino.h>

// Is this the first block of a real program for this chip? Returns nullptr if yes, otherwise a
// sentence saying what is wrong. Used for downloaded files and for files sent from the browser.
const char *fwCheckHeader(const uint8_t *b, size_t n);

// The version this firmware carries inside itself. The same text is searched for in a downloaded
// file so the display can say which version it just installed.
const char *fwVersionText();

// The saved firmware address (empty until the person enters one).
String fwSavedUrl();
// Trim and check an address. Returns nullptr if it is usable, otherwise why not.
const char *fwCleanUrl(String &url);
void fwSaveUrl(const String &url);

// Look at the file at the address (headers only, a few seconds) and say whether it differs from
// the one installed last time from that address. ok=false when the address could not be read.
String fwCheck(const String &url, bool &ok);

// Start downloading and installing in the background. md5 may be empty (or 32 hex characters, and
// then the file must match it). Returns false with a reason if it could not start.
bool fwStart(const String &url, const String &md5, String &why);

enum FwState { FW_IDLE = 0, FW_RUNNING, FW_DONE, FW_FAILED };
struct FwStatus {
  FwState  state;
  uint8_t  pct;                 // 0..100 while running
  uint32_t bytes, total;
  char     msg[128];            // progress text, the result, or the reason it failed
};
FwStatus fwStatus();

// Explicit opt-in automatic updates. The updater only ever uses the fixed,
// official PixelPop firmware URL; it never uses an address entered in the UI.
bool   fwAutoEnabled();
void   fwAutoSetEnabled(bool enabled);
bool   fwAutoCheckNow();
String fwAutoStatus();
void   fwAutoTick();

// The Home page: checks the same fixed, official PixelPop GitHub repo as the automatic updates
// above, but only offers to install rather than installing by itself. Sets msg to a short sentence
// either way; returns true only when it is confidently a different file than the one last installed
// from that address over Wi-Fi (a board flashed over USB has never installed anything that way, so
// bytes alone cannot say whether it is current, and the Home page should not nag every visit).
bool fwCheckOfficial(String &msg);
bool fwStartOfficial(String &why);           // download and install that same official file

// Called from the main loop (webLoop): shows the progress and the result on the panel.
void fwPanelTick();
