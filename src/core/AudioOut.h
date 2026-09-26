#pragma once
// I2S stream for the chime and siren: 16 bit stereo, 16 kHz, to the onboard codec (or, if you wired
// your own, a plain I2S amplifier). The codec and speaker pins live in
// core/Speaker.h.

#include <Arduino.h>
#include "Speaker.h"

#define AUDIO_SAMPLE_RATE 16000

struct AudioConfig {
  bool   codec;                    // true: onboard ES8311. false: plain I2S amplifier
  int8_t bclk, ws, dout;           // only used when codec == false
};

class AudioOut {
 public:
  // Starts the I2S stream (and the codec). msg receives a short status text.
  // amp = true switches the power amplifier on (false for a silent self-test).
  bool open(const AudioConfig &cfg, bool amp, char *msg, size_t msgLen);
  // Blocks until all frames are queued. Stereo interleaved 16-bit.
  void write(const int16_t *stereo, size_t frames);
  void close();

 private:
  bool _open = false;
  bool _codec = false;
  bool _ownsI2s = false;
};
