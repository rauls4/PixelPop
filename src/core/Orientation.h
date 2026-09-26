#pragma once
// Automatic orientation using the board's QMI8658 IMU.

#include <Arduino.h>

struct OrientationSample {
  bool    valid;
  int16_t x, y, z;
};

bool orientationBegin();
bool orientationAvailable();
uint8_t orientationAddress();
bool orientationCurrent(uint8_t &rotation);  // sample once and return the current stable rotation
bool orientationTick(uint8_t &rotation);  // true only when the stable orientation changed
OrientationSample orientationSample();
bool orientationTap();  // returns true once for a detected physical tap
