#pragma once
// The onboard I2C bus (GPIO 47 = SDA, GPIO 48 = SCL) is shared by the audio codec and the
// temperature/humidity sensor. Anything that starts, uses or stops the bus takes this lock first,
// so the speaker code and the sensor code never pull the bus away from each other.

#include <Arduino.h>

#define I2C_PIN_SDA 47                 // from Waveshare's own sensor example for this board
#define I2C_PIN_SCL 48

bool i2cLock(unsigned long waitMs);    // true if you got it (give it back with i2cUnlock)
void i2cUnlock();
