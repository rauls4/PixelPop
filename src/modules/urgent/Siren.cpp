#include "Siren.h"
#include <math.h>
#include "../../core/AudioOut.h"

static const float HI_HZ = 880.0f;
static const float LO_HZ = 660.0f;
static const uint32_t TONE_MS = 550;              // each tone lasts this long

void SirenSynth::start(float gain) {
  _gain = gain < 0 ? 0 : (gain > 1 ? 1 : gain);
  _phase = 0; _freq = HI_HZ; _env = 0; _n = 0; _stopping = false; _done = false;
}

void SirenSynth::render(int16_t *out, size_t frames) {
  const float sr = (float)AUDIO_SAMPLE_RATE;
  const uint32_t toneSamples = (uint32_t)(AUDIO_SAMPLE_RATE / 1000UL) * TONE_MS;
  const float upStep = 1.0f / (0.020f * sr);       // 20 ms fade in
  const float downStep = 1.0f / (0.040f * sr);     // 40 ms fade out
  for (size_t i = 0; i < frames; i++) {
    const float target = ((_n / toneSamples) & 1) ? LO_HZ : HI_HZ;
    _freq += (target - _freq) * 0.004f;            // about a 15 ms glide
    _phase += 2.0f * (float)M_PI * _freq / sr;
    if (_phase > 2.0f * (float)M_PI) _phase -= 2.0f * (float)M_PI;
    if (_stopping) { _env -= downStep; if (_env <= 0) { _env = 0; _done = true; } }
    else if (_env < 1.0f) { _env += upStep; if (_env > 1.0f) _env = 1.0f; }
    // odd harmonics up to the 7th: a bright, buzzy tone (7 x 880 Hz is still under 8 kHz)
    const float s = sinf(_phase) + sinf(3 * _phase) / 3.0f + sinf(5 * _phase) / 5.0f + sinf(7 * _phase) / 7.0f;
    const float v = s * 0.78f * _env * _gain;
    int32_t q = (int32_t)(v * 32000.0f);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    out[2 * i] = out[2 * i + 1] = (int16_t)q;
    _n++;
  }
}
