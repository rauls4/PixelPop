#pragma once
// The siren of the urgent notice: two tones that alternate (the "hi-lo" of European emergency
// vehicles), built from a square-ish wave so it carries on the small speaker. 16 kHz, stereo.

#include <Arduino.h>

class SirenSynth {
 public:
  void start(float gain);                         // gain 0..1
  void stop() { _stopping = true; }               // fades out, then finished() is true
  bool finished() const { return _done; }
  void render(int16_t *stereo, size_t frames);    // interleaved L,R

 private:
  float    _gain = 0.5f;
  float    _phase = 0;                            // radians
  float    _freq = 880.0f;                        // glides to the target tone, so tone changes do not click
  float    _env = 0;                              // 0..1 fade in / out
  uint32_t _n = 0;                                // samples since the start
  bool     _stopping = false, _done = false;
};
