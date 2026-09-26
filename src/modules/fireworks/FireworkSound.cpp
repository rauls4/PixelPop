#include "FireworkSound.h"
#include <math.h>
#include <string.h>

static const float TWO_PI_F = 6.2831853f;

float FireworkSynth::noise() {                 // xorshift32
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return ((int32_t)_rng) * (1.0f / 2147483648.0f);
}

float FireworkSynth::chance() {
  return (noise() + 1.0f) * 0.5f;
}

void FireworkSynth::reset() {
  memset(_v, 0, sizeof(_v));
}

bool FireworkSynth::idle() const {
  for (int i = 0; i < MAXV; i++) if (_v[i].on) return false;
  return true;
}

void FireworkSynth::trigger(FwSound s, float level, float seconds) {
  // free voice, else replace the oldest
  int idx = 0;
  uint32_t oldest = 0;
  for (int i = 0; i < MAXV; i++) {
    if (!_v[i].on) { idx = i; break; }
    if (_v[i].age >= oldest) { oldest = _v[i].age; idx = i; }
  }
  Voice &v = _v[idx];
  memset(&v, 0, sizeof(v));
  v.on = true;
  v.type = s;
  v.level = level;
  switch (s) {
    case FW_LAUNCH:     v.life = (uint32_t)((seconds < 0.4f ? 0.4f : seconds) * SAMPLE_RATE); break;
    case FW_BOOM:       v.life = (uint32_t)(1.0f * SAMPLE_RATE); break;
    case FW_BOOM_SOFT:  v.life = (uint32_t)(0.7f * SAMPLE_RATE); break;
    case FW_CRACKLE:    v.life = (uint32_t)(1.5f * SAMPLE_RATE); break;
  }
}

void FireworkSynth::render(int16_t *out, size_t frames, float gain) {
  const float fs = (float)SAMPLE_RATE;
  for (size_t n = 0; n < frames; n++) {
    float acc = 0;
    for (int i = 0; i < MAXV; i++) {
      Voice &v = _v[i];
      if (!v.on) continue;
      float t = v.age / fs;                    // seconds since the start
      float p = (float)v.age / (float)v.life;  // 0..1 through the sound
      float s = 0;

      switch (v.type) {
        case FW_LAUNCH: {
          // filtered noise that brightens as the rocket climbs, plus a thin rising whistle
          float a = 0.03f + 0.40f * p * p;            // low-pass coefficient (cutoff rises)
          v.lp += a * (noise() - v.lp);
          float env = (p < 0.75f) ? (p / 0.75f) : (1.0f - (p - 0.75f) / 0.25f);
          env = env * env;
          float f = 700.0f + 1100.0f * p;             // whistle 700 -> 1800 Hz
          v.phase += TWO_PI_F * f / fs;
          if (v.phase > TWO_PI_F) v.phase -= TWO_PI_F;
          s = env * (2.2f * v.lp + 0.10f * sinf(v.phase));
          break;
        }
        case FW_BOOM:
        case FW_BOOM_SOFT: {
          float k = (v.type == FW_BOOM) ? 1.0f : 0.65f;
          // low thump: a sine that falls in pitch
          float f = 55.0f + 130.0f * expf(-t / 0.10f);
          v.phase += TWO_PI_F * f / fs;
          if (v.phase > TWO_PI_F) v.phase -= TWO_PI_F;
          float thump = sinf(v.phase) * expf(-t / 0.28f);
          // rumble: low-passed noise that fades more slowly
          v.lp += 0.10f * (noise() - v.lp);
          float rumble = v.lp * 3.0f * expf(-t / 0.30f);
          // crack: a very short burst of bright noise at the start
          float crack = noise() * expf(-t / 0.018f);
          s = k * (0.9f * thump + rumble + 0.6f * crack);
          break;
        }
        case FW_CRACKLE: {
          // random little clicks whose rate and loudness die away
          float rate = 0.0030f * (1.0f - p);          // chance per sample
          if (chance() < rate) v.click = 0.3f + 0.7f * chance();
          s = noise() * v.click * 0.8f;
          v.click *= 0.88f;
          break;
        }
      }
      acc += s * v.level;
      if (++v.age >= v.life) v.on = false;
    }
    float x = acc * 0.45f * gain;
    if (x > 0.95f) x = 0.95f;
    if (x < -0.95f) x = -0.95f;
    int16_t s16 = (int16_t)(x * 32767.0f);
    out[2 * n] = s16;
    out[2 * n + 1] = s16;
  }
}
