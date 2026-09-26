#pragma once
// Minimal driver for the ES8311 audio codec (speaker output only).
// Written from the codec's register map / the usual Espressif init sequence;
// it has NOT been tested on this board.

#include <Arduino.h>

// Probe the I2C bus for the codec. Returns its 7-bit address (0x18 or 0x19),
// or 0 if not found. `seen` receives a text list of every device that answered.
uint8_t es8311Probe(String &seen);

// Configure for 16-bit stereo I2S in slave mode with MCLK = 256 x sample rate
// (the same register values work for any rate; only the DAC oversampling
// differs: 0x20 for rates up to 24 kHz, 0x10 above). The I2S clocks
// (especially MCLK) should already be running.
bool es8311Init(uint8_t addr, uint8_t dacVolumeReg, uint8_t dacOsr);

void es8311Mute(uint8_t addr, bool mute);
