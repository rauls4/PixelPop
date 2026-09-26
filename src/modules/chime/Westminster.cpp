#include "Westminster.h"
#include <math.h>
#include <string.h>

namespace {

// Notes (Hz)
const float B3 = 246.94f, E3 = 164.81f, E4 = 329.63f, Fs4 = 369.99f, Gs4 = 415.30f;

// The five "changes" of the Westminster Quarters, four notes each.
const float CHANGE[5][4] = {
  {Gs4, Fs4, E4,  B3},    // 1
  {E4,  Gs4, Fs4, B3},    // 2
  {E4,  Fs4, Gs4, E4},    // 3
  {Gs4, E4,  Fs4, B3},    // 4
  {B3,  Fs4, Gs4, E4},    // 5
};

// Which changes are played at each point of the hour.
const int SEQ_Q1[]   = {1};
const int SEQ_HALF[] = {2, 3};
const int SEQ_Q3[]   = {4, 5, 1};
const int SEQ_HOUR[] = {2, 3, 4, 5};

// A church bell is not a pure tone: it has several inharmonic partials that
// die away at different speeds (higher ones faster).
const float PARTIAL_RATIO[5] = {1.0f, 2.0f, 2.4f, 3.0f, 4.07f};
const float PARTIAL_AMP[5]   = {1.0f, 0.6f, 0.35f, 0.25f, 0.15f};
const float PARTIAL_T60[5]   = {3.0f, 2.0f, 1.4f, 1.0f, 0.6f};   // seconds to fade by 60 dB

// A cuckoo's whistle: a strong fundamental with a faint second and third harmonic
const float WHISTLE_RATIO[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
const float WHISTLE_AMP[5]   = {1.0f, 0.16f, 0.05f, 0.0f, 0.0f};

const float NOTE_SEC   = 0.90f;   // time between notes
const float PHRASE_GAP = 0.35f;   // extra pause between phrases
const float STRIKE_GAP = 1.60f;   // time between hour strikes
const float PRE_STRIKE = 1.30f;   // pause before the hour strikes begin
const float RING_SEC   = 3.2f;    // how long one bell rings
const uint32_t ATTACK  = 64;      // samples of fade-in (avoids a click)

// Cuckoo call: two whistled notes, a falling major third (about F#5 then D5)
const float CUCKOO_HI = 739.99f, CUCKOO_LO = 587.33f;
const float CUCKOO_NOTE1 = 0.26f;       // seconds held
const float CUCKOO_NOTE2 = 0.42f;
const float CUCKOO_STEP  = 0.30f;       // second note starts this long after the first
const float CUCKOO_PERIOD = 1.25f;      // time from one call to the next
const uint32_t WHISTLE_ATTACK  = 200;   // 12 ms fade-in
const uint32_t WHISTLE_RELEASE = 700;   // 44 ms fade-out

}  // namespace

void BellSynth::addEvent(uint32_t at, float freq, float amp, uint32_t dur) {
  if (_nEv >= MAXE) return;
  _ev[_nEv++] = {at, freq, amp, dur};
}

void BellSynth::start(ChimeStyle style, ChimeKind kind, int hour12, float gain) {
  memset(_v, 0, sizeof(_v));
  _nEv = 0; _next = 0; _pos = 0;
  _gain = gain < 0 ? 0 : (gain > 1 ? 1 : gain);

  if (style == STYLE_CUCKOO) {
    const float fsr = (float)SAMPLE_RATE;
    int calls = 1;
    if (kind == CHIME_HOUR) {
      calls = hour12 < 1 ? 1 : (hour12 > 12 ? 12 : hour12);
    }
    for (int i = 0; i < calls; i++) {
      float t = i * CUCKOO_PERIOD;
      addEvent((uint32_t)(t * fsr), CUCKOO_HI, 1.8f, (uint32_t)(CUCKOO_NOTE1 * fsr));
      addEvent((uint32_t)((t + CUCKOO_STEP) * fsr), CUCKOO_LO, 1.8f, (uint32_t)(CUCKOO_NOTE2 * fsr));
    }
    return;
  }

  const bool grandfather = style == STYLE_GRANDFATHER;
  const float pitch = grandfather ? 0.78f : 1.0f;
  const float noteGap = grandfather ? 1.12f : NOTE_SEC;
  const float phraseGap = grandfather ? 0.50f : PHRASE_GAP;
  const float strikeGap = grandfather ? 1.90f : STRIKE_GAP;

  const int *seq = SEQ_Q1; int n = 1;
  switch (kind) {
    case CHIME_TICK:
    case CHIME_QUARTER1: seq = SEQ_Q1;   n = 1; break;
    case CHIME_HALF:     seq = SEQ_HALF; n = 2; break;
    case CHIME_QUARTER3: seq = SEQ_Q3;   n = 3; break;
    case CHIME_HOUR:     seq = SEQ_HOUR; n = 4; break;
  }

  const float fs = (float)SAMPLE_RATE;
  float t = 0;
  for (int p = 0; p < n; p++) {
    for (int i = 0; i < 4; i++) {
      addEvent((uint32_t)(t * fs), CHANGE[seq[p] - 1][i] * pitch, 1.0f);
      t += noteGap;
    }
    t += phraseGap;
  }

  if (kind == CHIME_HOUR) {
    if (hour12 < 1) hour12 = 1;
    if (hour12 > 12) hour12 = 12;
    t += PRE_STRIKE;
    for (int s = 0; s < hour12; s++) {
      addEvent((uint32_t)(t * fs), E3 * pitch, 1.3f);      // the deep hour bell
      t += strikeGap;
    }
  }
}

void BellSynth::spawn(const Event &e) {
  // free voice, else steal the oldest one
  int idx = -1;
  uint32_t oldest = 0;
  for (int i = 0; i < MAXV; i++) {
    if (!_v[i].on) { idx = i; break; }
    if (_v[i].age >= oldest) { oldest = _v[i].age; idx = i; }
  }
  Voice &v = _v[idx];
  v.on = true;
  v.age = 0;
  v.amp = e.amp;
  v.dur = e.dur;
  v.life = e.dur ? e.dur + WHISTLE_RELEASE : (uint32_t)(RING_SEC * SAMPLE_RATE);
  const float fs = (float)SAMPLE_RATE;
  for (int p = 0; p < NP; p++) {
    float ratio = e.dur ? WHISTLE_RATIO[p] : PARTIAL_RATIO[p];
    float w = 2.0f * 3.14159265f * e.freq * ratio / fs;
    v.k[p]  = 2.0f * cosf(w);
    v.y1[p] = -sinf(w);            // so that the first output sample is sin(0)
    v.y2[p] = -sinf(2.0f * w);
    if (e.dur) {                   // whistle: nearly pure tone, no decay
      v.a[p] = WHISTLE_AMP[p];
      v.d[p] = 1.0f;
    } else {                       // bell
      v.a[p] = PARTIAL_AMP[p];
      v.d[p] = powf(0.001f, 1.0f / (PARTIAL_T60[p] * fs));   // -60 dB after T60
    }
  }
}

void BellSynth::render(int16_t *out, size_t frames) {
  const float base = 0.30f;         // headroom for several bells at once

  for (size_t i = 0; i < frames; i++) {
    while (_next < _nEv && _ev[_next].at <= _pos) spawn(_ev[_next++]);

    float acc = 0;
    for (int vi = 0; vi < MAXV; vi++) {
      Voice &v = _v[vi];
      if (!v.on) continue;
      float s = 0;
      for (int p = 0; p < NP; p++) {
        float y = v.k[p] * v.y1[p] - v.y2[p];
        v.y2[p] = v.y1[p];
        v.y1[p] = y;
        s += v.a[p] * y;
        v.a[p] *= v.d[p];
      }
      float env;
      if (v.dur) {                                   // whistle: quick rise, hold, quick fall
        env = 1.0f;
        if (v.age < WHISTLE_ATTACK) env = (float)v.age / (float)WHISTLE_ATTACK;
        else if (v.age >= v.dur) env = 1.0f - (float)(v.age - v.dur) / (float)WHISTLE_RELEASE;
        if (env < 0) env = 0;
      } else {                                       // bell: quick fade-in, then it rings out
        env = v.age < ATTACK ? (float)v.age / (float)ATTACK : 1.0f;
      }
      acc += s * env * v.amp;
      if (++v.age >= v.life) v.on = false;
    }

    float x = acc * base * _gain;
    if (x > 0.95f) x = 0.95f;
    if (x < -0.95f) x = -0.95f;
    int16_t s16 = (int16_t)(x * 32767.0f);
    out[2 * i] = s16;
    out[2 * i + 1] = s16;
    _pos++;
  }
}

bool BellSynth::finished() const {
  if (_next < _nEv) return false;
  for (int i = 0; i < MAXV; i++) if (_v[i].on) return false;
  return true;
}

float BellSynth::durationSeconds() const {
  if (_nEv == 0) return 0;
  const Event &last = _ev[_nEv - 1];
  float tail = last.dur ? (float)(last.dur + WHISTLE_RELEASE) / (float)SAMPLE_RATE : RING_SEC;
  return (float)last.at / (float)SAMPLE_RATE + tail;
}
