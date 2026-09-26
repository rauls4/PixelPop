#pragma once
// Base class for every function the display can perform (weather, clock,
// countdown, chimes, ...). A module lives in its own folder under
// src/modules/<name>/ and provides:
//   * what to draw on the panel (pages),
//   * background work (update),
//   * its own settings page on the web UI (onSettingsHtml / onSave),
//   * a persistent on/off switch and rotation time (handled here).
//
// To add a new function: create a folder with a class derived from Module,
// then add one line in PixelPop.ino to register it.

#include <Arduino.h>
#include <WebServer.h>
#include "Settings.h"

class Module {
 public:
  // prefix: short unique key prefix for flash storage, e.g. "w_"
  Module(const char *id, const char *title, const char *prefix,
         bool defEnabled, uint8_t defSeconds);
  virtual ~Module() {}

  const char *id() const { return _id; }          // also the web path: /<id>
  const char *title() const { return _title; }
  // Every module has its own version, MAJOR.MINOR.PATCH: PATCH for fixes, MINOR for new features or
  // settings, MAJOR when saved settings stop working. Shown on the module's page and on Advanced;
  // write the change in CHANGELOG.md.
  virtual const char *version() const = 0;
  bool        enabled() const { return _enabled; }
  uint8_t     seconds() const { return _seconds; }
  bool        setEnabled(bool on);                 // persists immediately

  void   load();                                   // read saved settings
  void   save(WebServer &server);                  // read the settings form
  String settingsHtml();                           // the module's form

  // ---- lifecycle ----
  virtual void begin() {}                          // after Wi-Fi is connected
  virtual void update() {}                         // every loop iteration
  virtual void refreshNow() {}                     // "refresh" requested
  virtual void netTick() {}                        // ~4 times a second from the shared network task (downloads go here)
  virtual void registerRoutes(WebServer &server) { (void)server; }  // extra web routes

  // ---- display (modules without a page return false from hasPages) ----
  virtual bool hasPages() { return true; }
  virtual int  pageCount() { return 1; }
  virtual bool pageAvailable(int sub) { (void)sub; return false; }
  virtual void drawPage(int sub) { (void)sub; }
  virtual bool needsRedraw() { return false; }     // e.g. clock: second changed
  // Return a transition style from Transition.cpp for this module's pages, or -1
  // to use the globally selected transition.
  virtual int  transitionOverride() { return -1; }
  // A page that decides by itself when it is finished (In Season: after the last item has scrolled by).
  // 0 = use the module's rotation time, 1 = still showing, do not move on yet, 2 = finished, move on now.
  virtual int  pageProgress() { return 0; }
  // Called by the rotation each time this page is about to be shown afresh (it just came round, or the display
  // woke up), before the first drawPage of that showing. A page that scrolls uses it to start from the beginning.
  virtual void pageEntered(int sub) { (void)sub; }
  // An urgent module can take over the whole panel while this returns true: the rotation stops and
  // drawPage(0) is shown until it returns false (see App::loop). Only the urgent notice uses it.
  virtual bool takesScreen() { return false; }

  // ---- web UI text ----
  virtual const char *enabledLabel() { return "Show in the rotation"; }
  virtual String summary() { return ""; }          // short status on the main page
  virtual String actionsHtml() { return ""; }      // extra buttons under the form
  virtual bool showSettingsPreview() { return false; }

 protected:
  Store store;
  virtual void   onLoad() {}
  virtual String onSettingsHtml() = 0;             // form fields
  virtual void   onSave(WebServer &server) = 0;    // read + persist fields

 private:
  const char *_id;
  const char *_title;
  bool        _defEnabled;
  uint8_t     _defSeconds;
  bool        _enabled;
  uint8_t     _seconds;
};
