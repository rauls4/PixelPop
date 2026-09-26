#pragma once
// Plex helpers with no hardware in them (so they can be tested on a PC):
//   * a streaming reader for the JSON that a Plex Media Server sends (it only keeps the few fields we need),
//   * the wording for the ratings and for "added 3 days ago".

#include <stdint.h>
#include <stddef.h>

// ---------- the server's JSON ----------
struct PlexItem {
  char     key[12];             // library sections: their number
  char     ratingKey[16];       // movies: the server's id for the movie
  char     type[12];            // "movie", "show", ...
  char     title[96];
  char     genre[40];           // the first one or two genres, e.g. "Drama, Comedy"
  char     ratingImage[48];     // where the critic rating comes from, e.g. "rottentomatoes://image.rating.ripe"
  char     audienceImage[48];
  char     thumb[128];          // server-relative poster path
  int      year;
  int      genreN;              // genres collected so far
  int      viewCount;           // how many times it was watched; the server leaves it out until the first time
  float    rating, audience;    // 0 to 10
  bool     hasRating, hasAudience;
  uint32_t added;               // when it was added to the library (seconds since 1970)
  void clear();
};

typedef void (*PlexItemFn)(void *ctx, const PlexItem &item);

// Feed the reply one character at a time. Every entry of the "Metadata" (movies) or "Directory" (library
// sections) list is handed to the callback as soon as it is complete, so the reply can be any size.
class PlexJson {
 public:
  void begin(PlexItemFn fn, void *ctx);
  void feed(char c);
  bool sawContainer() const { return _sawContainer; }      // the reply had the "MediaContainer" every real reply has
  int  items() const { return _count; }

 private:
  static const int MAXD = 8;
  PlexItemFn _fn = nullptr; void *_ctx = nullptr;
  int   _depth = 0, _count = 0;
  bool  _sawContainer = false;
  char  _key[MAXD][24];         // the name a value at each depth has
  char  _ck[MAXD][24];          // the name the container at each depth was opened under
  bool  _inStr = false, _esc = false;
  int   _uni = 0; uint32_t _cp = 0, _hi = 0;
  char  _str[128]; int _strLen = 0;
  bool  _haveStr = false;       // a finished string that is not yet known to be a name or a value
  char  _tok[24]; int _tokLen = 0;
  PlexItem _it;
  bool  inItem() const;
  void  flush();
  void  addGenre(const char *tag);
  void  value(const char *text, bool isString);
  void  strChar(char c);
};

// ---------- wording ----------
enum PlexRatingKind { PR_NONE = 0, PR_RT_FRESH, PR_RT_ROTTEN, PR_POP_UP, PR_POP_SPILLED, PR_IMDB, PR_TMDB, PR_OTHER };
PlexRatingKind plexRatingKind(const char *image);
// "87%" for Rotten Tomatoes ratings, "8.1" for the others.
void plexRatingText(PlexRatingKind kind, float value, char *out, size_t cap);
// "TODAY", "3D AGO", "2W AGO", "5M AGO"; empty when the clock is not set.
void plexAgeText(uint32_t added, uint32_t now, char *out, size_t cap);
