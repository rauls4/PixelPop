#include "GasData.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static const char *const LABELS[GAS_WHEN_COUNT] = {"Current Avg", "Yesterday Avg", "Week Ago Avg", "Month Ago Avg", "Year Ago Avg"};

void AaaParser::begin() {
  memset(_win, 0, sizeof(_win));
  _winLen = 0;
  _inTag = false;
  _lab = -1;
  _gap = 0;
  _num = false;
  _nl = 0;
  for (int i = 0; i < GAS_WHEN_COUNT; i++) {
    _np[i] = 0;
    _fin[i] = false;
    for (int g = 0; g < GAS_GRADE_COUNT; g++) _v[i][g] = 0;
  }
}

bool AaaParser::done() const {
  for (int i = 0; i < GAS_WHEN_COUNT; i++) if (!_fin[i]) return false;
  return true;
}

void AaaParser::finishRow() {
  if (_lab >= 0 && _np[_lab] > 0) _fin[_lab] = true;
  _lab = -1;
  _num = false;
}

void AaaParser::endNumber() {
  _num = false;
  _nb[_nl] = 0;
  const float v = (float)atof(_nb);
  _nl = 0;
  if (_lab < 0 || v < 0.5f || v > 30.0f) return;                 // not a price per gallon
  if (_np[_lab] < GAS_GRADE_COUNT) _v[_lab][_np[_lab]++] = v;
  _gap = 0;
  if (_np[_lab] >= GAS_GRADE_COUNT) finishRow();
}

void AaaParser::feed(char c) {
  if (_inTag) { if (c == '>') _inTag = false; return; }
  if (c == '<') { _inTag = true; text(' '); return; }            // a tag separates words
  text(c);
}

void AaaParser::text(char c) {
  if ((unsigned char)c < ' ') c = ' ';
  if (c == ' ' && _winLen > 0 && _win[_winLen - 1] == ' ') return;          // one space is enough
  if (_winLen == WIN) { memmove(_win, _win + 1, WIN - 1); _winLen = WIN - 1; }
  _win[_winLen++] = c;
  _win[_winLen] = 0;
  if (c == ';' && _winLen >= 6 && strcmp(_win + _winLen - 6, "&nbsp;") == 0) {   // &nbsp; is a space
    _winLen -= 6;
    _win[_winLen] = 0;
    text(' ');
    return;
  }

  // a dollar amount being read
  if (_num) {
    if ((c >= '0' && c <= '9') || c == '.') { if (_nl < (int)sizeof(_nb) - 1) _nb[_nl++] = c; return; }
    endNumber();
  }
  if (c == '$' && _lab >= 0) { _num = true; _nl = 0; return; }
  if (_lab >= 0) {
    _gap++;
    if (_gap > (_np[_lab] > 0 ? 160 : 400)) finishRow();          // the row is over (or was not a row after all)
  }

  // a row label just ended ("...Avg" or "...Avg.")
  if (c == 'g') {
    for (int i = 0; i < GAS_WHEN_COUNT; i++) {
      const size_t n = strlen(LABELS[i]);
      if ((size_t)_winLen >= n && strncasecmp(_win + _winLen - n, LABELS[i], n) == 0) {
        if (_lab >= 0) finishRow();
        if (!_fin[i]) { _lab = i; _np[i] = 0; _gap = 0; }
        break;
      }
    }
  }
}

void gasFmtChange(char *out, size_t n, float delta) {
  if (fabsf(delta) < 0.0005f) snprintf(out, n, "0.000");
  else                        snprintf(out, n, "%+.3f", delta);
}
