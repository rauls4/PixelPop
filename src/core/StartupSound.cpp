#include "StartupSound.h"
#include <math.h>
#include "AudioOut.h"

static void playNote(AudioOut &audio, float hz, uint16_t ms) {
  static int16_t buf[128 * 2];
  const uint32_t frames = (uint32_t)AUDIO_SAMPLE_RATE * ms / 1000;
  float phase = 0;
  for (uint32_t done = 0; done < frames; ) {
    const uint32_t n = (frames - done) < 128 ? frames - done : 128;
    for (uint32_t i = 0; i < n; i++) {
      const float p = (float)(done + i) / (float)frames;
      const float envelope = sinf((float)M_PI * p);
      const float tone = sinf(phase) + 0.20f * sinf(2.0f * phase);
      const int16_t sample = (int16_t)(tone * envelope * 3500.0f);
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
      phase += 2.0f * (float)M_PI * hz / AUDIO_SAMPLE_RATE;
      if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    audio.write(buf, n);
    done += n;
  }
}

void startupPlayTune() {
  AudioOut audio;
  AudioConfig cfg = {true, 0, 0, 0};
  char msg[64];
  if (!audio.open(cfg, true, msg, sizeof(msg))) {
    Serial.printf("Startup tune: %s\n", msg);
    return;
  }

  static const float notes[] = {659.26f, 783.99f, 987.77f, 1318.51f, 1046.50f};
  for (unsigned i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) playNote(audio, notes[i], 105);
  audio.close();
}
