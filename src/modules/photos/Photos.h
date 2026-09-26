#pragma once
// Photo module: shows the pictures you upload from the settings page, one after another.
//
// Pictures are shrunk in your web browser to at most 128 pixels on the long side and sent to
// the board, which keeps them in its flash storage (LittleFS). The board fits them to the
// screen when it draws them, so you can change how they fit (scale to fit, crop, black bars)
// at any time without uploading again.

#include <LittleFS.h>
#include "../../core/Module.h"
#include "../../core/Config.h"
#include "PhotoData.h"

class PhotoModule : public Module {
 public:
  PhotoModule() : Module("photos", "Photos", "o_", true, 8) {}
  const char *version() const override { return "1.0.0"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void registerRoutes(WebServer &server) override;

  int  pageCount() override { return _count > 0 ? _count : 1; }
  bool pageAvailable(int sub) override { return sub >= 0 && sub < _count; }
  void drawPage(int sub) override;

  const char *enabledLabel() override { return "Show the photos in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  static const int MAX_PHOTOS = 16;

  uint8_t _fit = FIT_BARS;                 // how a picture is fitted to the screen
  uint8_t _bright = 100;                   // percent

  bool    _fsOk = false;                   // storage mounted
  bool    _has[MAX_PHOTOS] = {false};      // which slots hold a picture
  int     _count = 0;

  // the picture last drawn, already fitted to the screen
  uint16_t _frame[PANEL_W * PANEL_H];
  int      _cacheSlot = -1, _cacheW = 0, _cacheH = 0, _cacheFit = -1, _cacheBright = -1;

  // an upload in progress
  File     _upFile;
  int      _upSlot = -1;
  bool     _upOk = false, _upResult = false;
  uint8_t  _upHdr[4];
  int      _upGot = 0;
  uint32_t _upSize = 0;

  void   scan();
  int    slotOfPage(int sub) const;
  String path(int slot, bool temp) const;
  bool   loadFrame(int slot, int w, int h);
  void   onUpload(WebServer &server);
  void   invalidate() { _cacheSlot = -1; }
};
