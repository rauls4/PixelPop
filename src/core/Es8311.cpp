#include "Es8311.h"
#include <Wire.h>

static bool wr(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static int rd(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)addr, 1) != 1) return -1;
  return Wire.read();
}

uint8_t es8311Probe(String &seen) {
  seen = "";
  uint8_t found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      char b[8];
      snprintf(b, sizeof(b), "0x%02X ", a);
      seen += b;
      if ((a == 0x18 || a == 0x19) && !found) {
        // chip ID registers of the ES8311: 0xFD = 0x83, 0xFE = 0x11
        int id1 = rd(a, 0xFD), id2 = rd(a, 0xFE);
        if (id1 == 0x83 && id2 == 0x11) found = a;
      }
    }
  }
  return found;
}

bool es8311Init(uint8_t a, uint8_t vol, uint8_t dacOsr) {
  bool ok = true;
  ok &= wr(a, 0x00, 0x1F);            // reset
  delay(20);
  ok &= wr(a, 0x00, 0x00);
  ok &= wr(a, 0x00, 0x80);            // power on, slave mode
  ok &= wr(a, 0x01, 0x3F);            // all clocks on, MCLK from the MCLK pin
  ok &= wr(a, 0x02, 0x00);            // pre-divider 1, multiplier 1
  ok &= wr(a, 0x03, 0x10);            // single-speed mode, ADC OSR
  ok &= wr(a, 0x04, dacOsr);          // DAC OSR
  ok &= wr(a, 0x05, 0x00);            // ADC / DAC dividers = 1
  ok &= wr(a, 0x06, 0x03);            // BCLK divider (unused in slave mode)
  ok &= wr(a, 0x07, 0x00);            // LRCK divider high
  ok &= wr(a, 0x08, 0xFF);            // LRCK divider low (256)
  ok &= wr(a, 0x09, 0x0C);            // DAC serial port: I2S, 16-bit
  ok &= wr(a, 0x0A, 0x0C);            // ADC serial port: I2S, 16-bit
  ok &= wr(a, 0x0D, 0x01);            // power up analog circuitry
  ok &= wr(a, 0x0E, 0x02);            // power up PGA / ADC modulator
  ok &= wr(a, 0x12, 0x00);            // power up DAC
  ok &= wr(a, 0x13, 0x10);            // enable the headphone / speaker driver
  ok &= wr(a, 0x1C, 0x6A);            // ADC equalizer bypass, DC cancel
  ok &= wr(a, 0x37, 0x08);            // DAC equalizer bypass
  ok &= wr(a, 0x44, 0x08);
  ok &= wr(a, 0x31, 0x00);            // un-mute
  ok &= wr(a, 0x32, vol);             // DAC volume (0xBF = 0 dB)
  return ok;
}

void es8311Mute(uint8_t a, bool mute) {
  wr(a, 0x31, mute ? 0x60 : 0x00);
}
