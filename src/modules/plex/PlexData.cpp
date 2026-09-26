#include "PlexData.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// =====================================================================================
//  JSON
// =====================================================================================
void PlexItem::clear() {
  key[0] = ratingKey[0] = type[0] = title[0] = genre[0] = ratingImage[0] = audienceImage[0] = thumb[0] = 0;
  year = 0; genreN = 0; viewCount = 0; rating = audience = 0; hasRating = hasAudience = false; added = 0;
}

void PlexJson::begin(PlexItemFn fn, void *ctx) {
  _fn = fn; _ctx = ctx;
  _depth = 0; _count = 0; _sawContainer = false;
  for (int i = 0; i < MAXD; i++) { _key[i][0] = 0; _ck[i][0] = 0; }
  _inStr = _esc = false; _uni = 0; _cp = _hi = 0;
  _strLen = 0; _haveStr = false; _tokLen = 0;
  _it.clear();
}

// Items sit at depth 4:  { "MediaContainer": { "Metadata": [ { ...item... } ] } }
bool PlexJson::inItem() const {
  return _depth == 4 && (!strcmp(_ck[3], "Metadata") || !strcmp(_ck[3], "Directory"));
}

static void copyTo(char *dst, size_t cap, const char *src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

// The genres of a movie sit one level deeper:  "Genre": [ { "tag": "Drama" }, ... ]. The first two are kept.
void PlexJson::addGenre(const char *tag) {
  if (!tag[0] || _it.genreN >= 2) return;
  const size_t have = strlen(_it.genre), add = strlen(tag);
  if (_it.genreN == 0) copyTo(_it.genre, sizeof(_it.genre), tag);
  else if (have + 2 + add < sizeof(_it.genre)) { strcat(_it.genre, ", "); strcat(_it.genre, tag); }
  else return;
  _it.genreN++;
}

void PlexJson::value(const char *text, bool isString) {
  if (_depth == 6 && isString && !strcmp(_key[6], "tag") && !strcmp(_ck[5], "Genre") && !strcmp(_ck[3], "Metadata")) { addGenre(text); return; }
  if (!inItem()) return;
  const char *k = _key[4];
  if (!k[0]) return;
  if (!strcmp(k, "ratingKey")) copyTo(_it.ratingKey, sizeof(_it.ratingKey), text);
  else if (!strcmp(k, "key")) copyTo(_it.key, sizeof(_it.key), text);
  else if (!strcmp(k, "type")) copyTo(_it.type, sizeof(_it.type), text);
  else if (!strcmp(k, "title")) copyTo(_it.title, sizeof(_it.title), text);
  else if (!strcmp(k, "ratingImage")) copyTo(_it.ratingImage, sizeof(_it.ratingImage), text);
  else if (!strcmp(k, "audienceRatingImage")) copyTo(_it.audienceImage, sizeof(_it.audienceImage), text);
  else if (!strcmp(k, "thumb")) copyTo(_it.thumb, sizeof(_it.thumb), text);
  else if (!strcmp(k, "year")) _it.year = atoi(text);
  else if (!strcmp(k, "viewCount") && !isString) _it.viewCount = atoi(text);
  else if (!strcmp(k, "rating") && !isString) { _it.rating = (float)atof(text); _it.hasRating = true; }
  else if (!strcmp(k, "audienceRating") && !isString) { _it.audience = (float)atof(text); _it.hasAudience = true; }
  else if (!strcmp(k, "addedAt")) _it.added = (uint32_t)strtoul(text, nullptr, 10);
}

// A finished string or bare value (number, true...) is handed on when the next , } ] shows what it was.
void PlexJson::flush() {
  if (_haveStr) {
    _str[_strLen] = 0;
    value(_str, true);
    _haveStr = false;
  } else if (_tokLen) {
    _tok[_tokLen] = 0;
    value(_tok, false);
  }
  _tokLen = 0;
}

static int utf8(uint32_t cp, char *o) {
  if (cp < 0x80) { o[0] = (char)cp; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
}

void PlexJson::strChar(char c) {
  if (_strLen < (int)sizeof(_str) - 4) _str[_strLen++] = c;
}

void PlexJson::feed(char c) {
  if (_inStr) {
    if (_uni > 0) {                                     // reading the 4 hex digits of \uXXXX
      int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0;
      _cp = (_cp << 4) | (uint32_t)v;
      if (--_uni == 0) {
        if (_cp >= 0xD800 && _cp < 0xDC00) _hi = _cp;                     // first half of a pair
        else {
          uint32_t cp = _cp;
          if (_cp >= 0xDC00 && _cp < 0xE000) cp = _hi ? 0x10000 + ((_hi - 0xD800) << 10) + (_cp - 0xDC00) : '?';
          _hi = 0;
          if (cp >= 0x10000) { strChar('?'); }             // beyond what the panel font has anyway
          else { char b[4]; int n = utf8(cp, b); for (int i = 0; i < n; i++) strChar(b[i]); }
        }
      }
      return;
    }
    if (_esc) {
      _esc = false;
      switch (c) {
        case 'n': case 't': case 'r': strChar(' '); break;
        case 'u': _uni = 4; _cp = 0; break;
        default: strChar(c); break;                     // \" \\ \/
      }
      return;
    }
    if (c == '\\') { _esc = true; return; }
    if (c == '"') { _inStr = false; _haveStr = true; return; }
    strChar(c);
    return;
  }
  switch (c) {
    case '"': _inStr = true; _strLen = 0; _haveStr = false; _tokLen = 0; break;
    case ':':                                             // the string before was a name
      if (_haveStr && _depth < MAXD) { _str[_strLen] = 0; copyTo(_key[_depth], sizeof(_key[_depth]), _str); }
      _haveStr = false; _tokLen = 0;
      break;
    case '{': case '[':
      if (_depth + 1 < MAXD) {
        copyTo(_ck[_depth + 1], sizeof(_ck[0]), _key[_depth]);
        _key[_depth + 1][0] = 0;
      }
      _depth++;
      if (_depth == 2 && !strcmp(_ck[2], "MediaContainer")) _sawContainer = true;
      if (c == '{' && inItem()) _it.clear();
      _haveStr = false; _tokLen = 0;
      break;
    case '}': case ']':
      flush();
      if (c == '}' && inItem()) {
        _count++;
        if (_fn) _fn(_ctx, _it);
      }
      if (_depth > 0) _depth--;
      break;
    case ',':
      flush();
      break;
    case ' ': case '\t': case '\r': case '\n':
      if (_tokLen) flush();
      break;
    default:                                              // number, true, false, null
      if (_tokLen < (int)sizeof(_tok) - 1) _tok[_tokLen++] = c;
      break;
  }
}

// =====================================================================================
//  wording
// =====================================================================================
PlexRatingKind plexRatingKind(const char *image) {
  if (!image || !image[0]) return PR_NONE;
  if (strstr(image, "rottentomatoes")) {
    if (strstr(image, "upright")) return PR_POP_UP;
    if (strstr(image, "spilled")) return PR_POP_SPILLED;
    if (strstr(image, "image.rating.rotten")) return PR_RT_ROTTEN;
    return PR_RT_FRESH;                                     // ripe, certified
  }
  if (strstr(image, "imdb")) return PR_IMDB;
  if (strstr(image, "themoviedb") || strstr(image, "tmdb")) return PR_TMDB;
  return PR_OTHER;
}

void plexRatingText(PlexRatingKind kind, float value, char *out, size_t cap) {
  if (kind == PR_RT_FRESH || kind == PR_RT_ROTTEN || kind == PR_POP_UP || kind == PR_POP_SPILLED) snprintf(out, cap, "%d%%", (int)lroundf(value * 10.0f));
  else snprintf(out, cap, "%.1f", (double)value);
}

void plexAgeText(uint32_t added, uint32_t now, char *out, size_t cap) {
  out[0] = 0;
  if (!added || now < 1700000000UL || now < added) return;
  const uint32_t days = (now - added) / 86400;
  if (days == 0) snprintf(out, cap, "TODAY");
  else if (days < 14) snprintf(out, cap, "%luD AGO", (unsigned long)days);
  else if (days < 60) snprintf(out, cap, "%luW AGO", (unsigned long)(days / 7));
  else snprintf(out, cap, "%luM AGO", (unsigned long)(days / 30));
}
