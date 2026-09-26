#pragma once
// Synthesized firework sounds: the whoosh of a rocket, the boom of a burst and
// the crackle of falling sparks. No hardware in here (renders 16-bit stereo audio),
// so it can be tested on any computer.

#include <stdint.h>
#include <stddef.h>

enum FwSound {
  FW_LAUNCH = 0,       // rocket rising (whoosh + thin whistle)
  FW_BOOM,             // big burst
  FW_BOOM_SOFT,        // smaller / ring burst
  FW_CRACKLE           // sparks crackling after a burst
};

class FireworkSynth {
 public:
  static const int SAMPLE_RATE = 16000;

  void reset();
  // level 0..1.5 (loudness), seconds = length of a LAUNCH whoosh (ignored by the others)
  void trigger(FwSound s, float level, float seconds = 1.0f);
  // Fills `frames` stereo frames (L,R interleaved). gain 0..1 is the master volume.
  void render(int16_t *out, size_t frames, float gain);
  bool idle() const;               // nothing is sounding

 private:
  static const int MAXV = 12;
  struct Voice {
    bool on;
    FwSound type;
    uint32_t age, life;
    float level;
    float lp;                      // noise filter state
    float phase;                   // whistle / thump oscillator phase
    float click;                   // crackle: current click amplitude
  };
  Voice    _v[MAXV];
  uint32_t _rng = 88172645u;

  float noise();                   // white noise -1..1
  float chance();                  // 0..1
};
