#include "Orientation.h"
#include <Wire.h>
#include "I2cBus.h"
#include "Speaker.h"

static const uint8_t REG_WHO_AM_I = 0x00;
static const uint8_t REG_CTRL1 = 0x02;
static const uint8_t REG_CTRL2 = 0x03;
static const uint8_t REG_CTRL3 = 0x04;
static const uint8_t REG_CTRL7 = 0x08;
static const uint8_t REG_AX_L = 0x35;

static bool gAvailable = false;
static uint8_t gAddress = 0;
static unsigned long gLastSample = 0;
static uint8_t gRotation = 0;
static uint8_t gCandidate = 0;
static uint8_t gCandidateSamples = 0;
static OrientationSample gSample = {false, 0, 0, 0};
static bool gTapPending = false;
static unsigned long gLastTap = 0;

static bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(gAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t *out, size_t count) {
  Wire.beginTransmission(gAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gAddress, (int)count) != (int)count) return false;
  for (size_t i = 0; i < count; i++) out[i] = Wire.read();
  return true;
}

static bool openBus(bool &ownBus) {
  if (!i2cLock(100)) return false;
  ownBus = !speakerCodecOpen();
  if (ownBus) Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, 400000);
  return true;
}

static void closeBus(bool ownBus) {
  if (ownBus) Wire.end();
  i2cUnlock();
}

bool orientationBegin() {
  bool ownBus = false;
  if (!openBus(ownBus)) return false;
  gAvailable = false;
  gAddress = 0;
  static const uint8_t addresses[] = {0x6A, 0x6B};
  for (uint8_t address : addresses) {
    gAddress = address;
    uint8_t who = 0;
    if (readRegs(REG_WHO_AM_I, &who, 1) && who == 0x05 &&
        writeReg(REG_CTRL1, 0x40) &&       // I2C address auto-increment
        writeReg(REG_CTRL2, 0x06) &&       // accelerometer: +/-2 g, 125 Hz
        writeReg(REG_CTRL3, 0x56) &&       // gyroscope: +/-512 dps, 112 Hz
        writeReg(REG_CTRL7, 0x03)) {       // enable accelerometer and gyroscope
      gAvailable = true;
      break;
    }
  }
  closeBus(ownBus);
  if (!gAvailable) Serial.println("IMU: QMI8658 was not detected at 0x6A or 0x6B");
  else Serial.printf("IMU: QMI8658 detected at 0x%02X\n", gAddress);
  return gAvailable;
}

bool orientationAvailable() { return gAvailable; }
uint8_t orientationAddress() { return gAddress; }
OrientationSample orientationSample() { return gSample; }

static bool readCurrentOrientation(uint8_t &rotation) {
  bool ownBus = false;
  if (!openBus(ownBus)) return false;
  uint8_t raw[6];
  const bool ok = readRegs(REG_AX_L, raw, sizeof(raw));
  closeBus(ownBus);
  if (!ok) return false;

  const int32_t ax = (int16_t)((raw[1] << 8) | raw[0]);
  const int32_t ay = (int16_t)((raw[3] << 8) | raw[2]);
  const int32_t az = (int16_t)((raw[5] << 8) | raw[4]);
  gSample = {true, (int16_t)ax, (int16_t)ay, (int16_t)az};
  const int64_t magnitudeSquared = (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
  // A firm tap briefly exceeds normal stationary gravity. Debouncing prevents one impact
  // from being reported as several taps while the board rings.
  if (magnitudeSquared > (int64_t)27000 * 27000 && millis() - gLastTap > 220) {
    gTapPending = true;
    gLastTap = millis();
  }
  const int32_t x = ax < 0 ? -ax : ax;
  const int32_t y = ay < 0 ? -ay : ay;
  if (x < 2000 && y < 2000) return false;  // flat or moving: keep the last stable layout

  // Board-specific calibration: X- is landscape/bottom down, Y+ portrait/bottom
  // left, X+ landscape/bottom up, and Y- portrait/bottom right.
  if (x > y * 13 / 10) rotation = ax >= 0 ? 2 : 0;
  else                  rotation = ay >= 0 ? 1 : 3;
  return true;
}

bool orientationCurrent(uint8_t &rotation) {
  if (!gAvailable || !readCurrentOrientation(rotation)) return false;
  gRotation = rotation;
  gCandidateSamples = 0;
  return true;
}

bool orientationTick(uint8_t &rotation) {
  if (!gAvailable || millis() - gLastSample < 20) return false;
  gLastSample = millis();

  uint8_t next = 0;
  if (!readCurrentOrientation(next)) return false;
  if (next == gRotation) {
    gCandidateSamples = 0;
    return false;
  }
  if (next != gCandidate) {
    gCandidate = next;
    gCandidateSamples = 1;
    return false;
  }
  if (++gCandidateSamples < 3) return false;

  gRotation = next;
  gCandidateSamples = 0;
  rotation = gRotation;
  return true;
}

bool orientationTap() {
  const bool tapped = gTapPending;
  gTapPending = false;
  return tapped;
}
