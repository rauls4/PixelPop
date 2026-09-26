#pragma once
// Environment math with no hardware in it (so it can be tested on a PC):
//   * decoding the SHTC3 temperature / humidity sensor's reply (with its checksum),
//   * dew point, and correcting for a sensor that reads warm,
//   * a plain-word comfort rating,
//   * a short history of readings for the trend graphs.

#include <stdint.h>

uint8_t envCrc8(const uint8_t *data, int n);                 // Sensirion CRC-8 (poly 0x31, init 0xFF)
// 6 bytes: temperature (2) + crc, humidity (2) + crc. False if a checksum is wrong.
bool    envDecodeShtc3(const uint8_t b[6], float &tempC, float &rh);

float envDewPointC(float tempC, float rh);                   // Magnus formula
float envRhAtTemp(float dewC, float tempC);                  // relative humidity the same air has at another temperature
// The sensor sits next to a warm chip: subtract offsetC from the temperature and work out what the
// humidity of the same air is at that cooler temperature.
void  envCorrect(float rawT, float rawRh, float offsetC, float &t, float &rh);

inline float envCtoF(float c) { return c * 1.8f + 32.0f; }

enum EnvLevel { ENV_GOOD = 0, ENV_FAIR = 1, ENV_POOR = 2 };
// A word for how the air feels (COMFY, DRY, HUMID, COLD, HOT ...) and how good that is.
const char *envComfort(float tempC, float rh, EnvLevel &level);

// The last few hours of readings, oldest first.
class EnvHistory {
 public:
  static const int N = 64;
  void  add(float tempC, float rh);
  int   count() const { return _n; }
  float temp(int i) const { return _t[(_head + i) % N] / 10.0f; }    // i = 0 is the oldest
  float rh(int i) const { return _h[(_head + i) % N] / 10.0f; }
 private:
  int16_t _t[N] = {0}, _h[N] = {0};
  int _head = 0, _n = 0;
};
