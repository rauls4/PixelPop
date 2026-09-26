#include "AudioOut.h"
#include <ESP_I2S.h>

static I2SClass i2s;

bool AudioOut::open(const AudioConfig &cfg, bool amp, char *msg, size_t msgLen) {
  if (_open) close();
  if (!speakerAudioTake(7000)) {
    snprintf(msg, msgLen, "Another sound is still using I2S");
    return false;
  }
  _ownsI2s = true;
  _codec = cfg.codec;

  int8_t bclk = cfg.codec ? AUDIO_PIN_BCLK : cfg.bclk;
  int8_t ws   = cfg.codec ? AUDIO_PIN_WS   : cfg.ws;
  int8_t dout = cfg.codec ? AUDIO_PIN_DOUT : cfg.dout;
  int8_t mclk = cfg.codec ? AUDIO_PIN_MCLK : -1;

  i2s.setPins(bclk, ws, dout, -1, mclk);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    snprintf(msg, msgLen, "I2S could not start");
    speakerAudioGive();
    _ownsI2s = false;
    return false;
  }

  if (cfg.codec) {
    // MCLK is running now, so the codec can be configured.
    if (!speakerCodecStart(AUDIO_SAMPLE_RATE, amp, msg, msgLen)) {
      i2s.end();
      speakerAudioGive();
      _ownsI2s = false;
      return false;
    }
  } else {
    snprintf(msg, msgLen, "Ready (I2S amplifier)");
  }

  _open = true;
  return true;
}

void AudioOut::write(const int16_t *stereo, size_t frames) {
  if (!_open) return;
  const uint8_t *p = (const uint8_t *)stereo;
  size_t left = frames * 4;
  int stalls = 0;
  while (left > 0 && stalls < 5) {
    size_t n = i2s.write(p, left);
    if (n == 0) {
      stalls++;
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    p += n;
    left -= n;
  }
}

void AudioOut::close() {
  if (!_open) return;
  // Push silence so the last buffer does not keep looping, then shut down.
  static int16_t silence[256 * 2];
  memset(silence, 0, sizeof(silence));
  for (int i = 0; i < 8; i++) write(silence, 256);
  if (_codec) speakerCodecStop();
  i2s.end();
  _open = false;
  if (_ownsI2s) {
    speakerAudioGive();
    _ownsI2s = false;
  }
}
