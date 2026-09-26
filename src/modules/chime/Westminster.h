#pragma once
// The three chime sounds, synthesized:
//   Westminster : the Westminster Quarters ("Big Ben") played on bells
//   Cuckoo      : a cuckoo-clock call ("cuc-koo", two whistled notes)
//   Grandfather : a slower, lower Westminster-style longcase clock
// No hardware in here: it just renders 16-bit stereo audio, so it can be
// tested on any computer. The chime module feeds the audio to the speaker.

#include <stdint.h>
#include <stddef.h>

enum ChimeStyle {
  STYLE_WESTMINSTER = 0,
  STYLE_CUCKOO = 1,
  STYLE_GRANDFATHER = 2
};

// When the sound is played. Westminster: 1 / 2 / 3 / 4 phrases + hour strikes.
// Cuckoo: one call for every kind except CHIME_HOUR, which calls once per hour.
enum ChimeKind {
  CHIME_TICK = 0,      // one short phrase (used for the "every minute" mode)
  CHIME_QUARTER1,      // :15  - 1 phrase
  CHIME_HALF,          // :30  - 2 phrases
  CHIME_QUARTER3,      // :45  - 3 phrases
  CHIME_HOUR           // :00  - 4 phrases, then one bell strike per hour
};

class BellSynth {
 public:
  static const int SAMPLE_RATE = 16000;

  // hour12: 1..12 (number of strikes / cuckoos for CHIME_HOUR). gain: 0..1 master level.
  void   start(ChimeStyle style, ChimeKind kind, int hour12, float gain);
  // Fills `frames` stereo frames (interleaved L,R). Silence once finished.
  void   render(int16_t *out, size_t frames);
  bool   finished() const;
  // Total length of the sequence in seconds (approximate, includes the ring-out)
  float  durationSeconds() const;

 private:
  static const int NP = 5;          // partials per bell
  static const int MAXV = 8;        // bells ringing at once
  static const int MAXE = 32;       // notes in one sequence

  // dur == 0: a bell (rings and fades by itself). dur > 0: a whistled note held for dur samples.
  struct Event { uint32_t at; float freq; float amp; uint32_t dur; };
  struct Voice {
    bool on;
    uint32_t age;
    uint32_t life;      // samples until the voice ends
    uint32_t dur;       // 0 for a bell
    float amp;
    float y1[NP], y2[NP], k[NP], a[NP], d[NP];
  };

  Event    _ev[MAXE];
  int      _nEv = 0, _next = 0;
  Voice    _v[MAXV];
  uint32_t _pos = 0;
  float    _gain = 0.5f;

  void addEvent(uint32_t at, float freq, float amp, uint32_t dur = 0);
  void spawn(const Event &e);
};
