#pragma once
// Plex module: the movies most recently added to your Plex Media Server, one page each, with the title,
// the genre and the ratings the server has for the movie (Rotten Tomatoes tomato and popcorn when it has
// them, else IMDb or TMDb). The title scrolls when it does not fit.
//
// It asks the server on your own network for its movie libraries and their newest additions (the
// server's normal web address, port 32400, plus your Plex token). Portrait display caches a small,
// server-transcoded poster preview for each movie. See PlexData.cpp for the parsing.

#include "../../core/Module.h"
#include "../../core/NetLock.h"
#include "PlexData.h"

class PlexModule : public Module {
 public:
  PlexModule() : Module("plex", "Plex Recently Added", "l_", true, 9) {}
  const char *version() const override { return "2.3.5"; }      // bump when this module changes (see CHANGELOG.md)

  void begin() override;
  void netTick() override;
  void refreshNow() override { _fetchNow = true; }
  void registerRoutes(WebServer &server) override;

  int  pageCount() override;
  bool pageAvailable(int sub) override;
  void drawPage(int sub) override;
  bool needsRedraw() override;                     // a long title scrolls
  int  pageProgress() override;                    // a scrolling title is shown all the way round

  const char *enabledLabel() override { return "Show the newest movies in the rotation"; }
  String summary() override;
  String actionsHtml() override;

 protected:
  void   onLoad() override;
  String onSettingsHtml() override;
  void   onSave(WebServer &server) override;

 private:
  // ---- settings ----
  String   _url = "", _token = "", _lib = "";
  uint8_t  _count = 6;                             // movies shown
  bool     _showCrit = true, _showAud = true, _showNew = true, _showGenre = true, _onlyUnwatched = true;
  uint8_t  _speed = 1;                             // title scroll: 0 slow, 1 normal, 2 fast
  uint8_t  _everyIdx = 1;                          // index into EVERY_MIN
  uint32_t _colTitle = 0xFFFFFF, _colGenre = 0x5CC8FF, _colRating = 0xFFD060;

  // ---- data (written by the background task, read by the display) ----
  static const int MAXM = 10;
  struct Movie {
    char     rk[16];
    char     genre[40];                            // "Drama, Comedy", plain ASCII
    char     label[104];                           // "Title (2024)", plain ASCII
    float    crit, aud;
    bool     hasCrit, hasAud;
    bool     unwatched;                            // nobody has played it yet (for the account the token belongs to)
    PlexRatingKind critKind, audKind;
    uint32_t added;
    uint8_t  *thumb = nullptr;
    size_t   thumbSize = 0;
  };
  SimpleLock _lock;
  Movie      _mv[MAXM];
  int        _mvN = 0;
  bool       _tried = false, _failed = false;
  int        _fails = 0;
  char       _status[112] = "";
  unsigned long _lastTry = 0;

  // ---- background task ----
  volatile bool _fetchNow = false;
  Movie     *_stage = nullptr;                     // the new list while it is being built (only exists during a download)
  bool   fetch();
  bool   fetchInner();
  bool   fetchThumbnail(const char *path, uint8_t *&data, size_t &size);
  void   freeThumbnails(Movie *movies, int count);
  void   setStatus(const char *s);
  String baseUrl() const;
  int    httpGet(const String &url, void (*sink)(void *, const uint8_t *, size_t), void *ctx, size_t maxBytes);

  // ---- display ----
  int           _curSub = -1;
  unsigned long _t0 = 0, _lastSeen = 0, _totalMs = 0, _stepMs = 45;
  int           _lapPx = 0, _lastOffset = -1;
  bool          _scrollable = false;
  unsigned long msPerPx() const;
};
