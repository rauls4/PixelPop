#include "EnvData.h"
#include <math.h>

uint8_t envCrc8(const uint8_t *d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool envDecodeShtc3(const uint8_t b[6], float &tempC, float &rh) {
  if (envCrc8(b, 2) != b[2] || envCrc8(b + 3, 2) != b[5]) return false;
  const uint16_t rt = (uint16_t)((b[0] << 8) | b[1]);
  const uint16_t rr = (uint16_t)((b[3] << 8) | b[4]);
  tempC = -45.0f + 175.0f * (float)rt / 65536.0f;
  rh = 100.0f * (float)rr / 65536.0f;
  if (rh < 0) rh = 0;
  if (rh > 100) rh = 100;
  return true;
}

static const float MAG_A = 17.62f, MAG_B = 243.12f;

float envDewPointC(float t, float rh) {
  if (rh < 1.0f) rh = 1.0f;
  const float g = logf(rh / 100.0f) + MAG_A * t / (MAG_B + t);
  return MAG_B * g / (MAG_A - g);
}

float envRhAtTemp(float dew, float t) {
  float rh = 100.0f * expf(MAG_A * dew / (MAG_B + dew) - MAG_A * t / (MAG_B + t));
  if (rh < 0) rh = 0;
  if (rh > 100) rh = 100;
  return rh;
}

void envCorrect(float rawT, float rawRh, float offsetC, float &t, float &rh) {
  t = rawT - offsetC;
  if (offsetC == 0.0f) { rh = rawRh; return; }
  rh = envRhAtTemp(envDewPointC(rawT, rawRh), t);
}

const char *envComfort(float t, float rh, EnvLevel &lvl) {
  lvl = ENV_POOR;
  if (rh < 25) return "VERY DRY";
  if (rh > 70) return "VERY HUMID";
  if (t < 10) return "COLD";
  if (t > 32) return "HOT";
  lvl = ENV_FAIR;
  if (rh < 30) return "DRY";
  if (rh > 60) return "HUMID";
  if (t < 18) return "COOL";
  if (t > 28) return "HOT";
  if (t > 26) return "WARM";
  lvl = ENV_GOOD;
  return "COMFY";
}

void EnvHistory::add(float t, float rh) {
  const int16_t ti = (int16_t)lroundf(t * 10.0f), hi = (int16_t)lroundf(rh * 10.0f);
  if (_n < N) { _t[(_head + _n) % N] = ti; _h[(_head + _n) % N] = hi; _n++; }
  else { _t[_head] = ti; _h[_head] = hi; _head = (_head + 1) % N; }
}
