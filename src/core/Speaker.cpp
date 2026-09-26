#include "Speaker.h"
#include <Wire.h>
#include "freertos/semphr.h"
#include "Es8311.h"
#include "I2cBus.h"

static const uint8_t DAC_VOLUME_REG = 0xB0;     // a little below 0 dB; volume is applied in software
static uint8_t sAddr = 0;

static volatile bool sFireworksActive = false;
static volatile bool sChimeRequest = false;
static volatile bool sUrgent = false;
static StaticSemaphore_t sAudioMutexStorage;
static SemaphoreHandle_t sAudioMutex = xSemaphoreCreateMutexStatic(&sAudioMutexStorage);

static bool startLocked(uint32_t sampleRate, bool amp, char *msg, size_t msgLen) {
  pinMode(AUDIO_PIN_PA, OUTPUT);
  digitalWrite(AUDIO_PIN_PA, LOW);

  String seen;
  sAddr = 0;
  const int8_t order[2][2] = {{AUDIO_PIN_SDA, AUDIO_PIN_SCL}, {AUDIO_PIN_SCL, AUDIO_PIN_SDA}};
  for (int i = 0; i < 2 && !sAddr; i++) {
    Wire.begin(order[i][0], order[i][1], 100000);
    sAddr = es8311Probe(seen);
    if (!sAddr) Wire.end();
  }
  if (!sAddr) {
    snprintf(msg, msgLen, "ES8311 codec not found (I2C devices: %s)", seen.length() ? seen.c_str() : "none");
    return false;
  }
  if (!es8311Init(sAddr, DAC_VOLUME_REG, 0x20)) {
    snprintf(msg, msgLen, "ES8311 found at 0x%02X but setup failed", sAddr);
    Wire.end();
    sAddr = 0;
    return false;
  }
  if (amp) {
    digitalWrite(AUDIO_PIN_PA, HIGH);
    delay(20);
  }
  snprintf(msg, msgLen, "Ready (ES8311 at 0x%02X)", sAddr);
  return true;
}

bool speakerCodecStart(uint32_t sampleRate, bool amp, char *msg, size_t msgLen) {
  const bool got = i2cLock(3000);                 // the sensor module shares this bus
  if (!got) {
    snprintf(msg, msgLen, "Audio I2C bus is busy");
    return false;
  }
  const bool ok = startLocked(sampleRate, amp, msg, msgLen);
  i2cUnlock();
  return ok;
}

bool speakerCodecOpen() { return sAddr != 0; }

void speakerCodecStop() {
  const bool got = i2cLock(3000);
  if (!got) {
    Serial.println("Audio I2C bus did not release; codec left unchanged");
    return;
  }
  digitalWrite(AUDIO_PIN_PA, LOW);
  if (sAddr) {
    es8311Mute(sAddr, true);
    Wire.end();
    sAddr = 0;
  }
  i2cUnlock();
}

void speakerChimeRequest(bool on) { sChimeRequest = on; }
bool speakerChimeWaiting() { return sChimeRequest || sUrgent; }
bool speakerChimeBusy() { return sChimeRequest; }
void speakerUrgent(bool on) { sUrgent = on; }
bool speakerUrgentIsOn() { return sUrgent; }

void speakerFireworksActive(bool on) { sFireworksActive = on; }
bool speakerFireworksIsPlaying() { return sFireworksActive; }

void speakerAmp(bool on) { digitalWrite(AUDIO_PIN_PA, on ? HIGH : LOW); }
bool speakerAudioTake(unsigned long waitMs) {
  return sAudioMutex && xSemaphoreTake(sAudioMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
void speakerAudioGive() {
  if (sAudioMutex) xSemaphoreGive(sAudioMutex);
}
bool speakerAudioIsBusy() {
  return !sAudioMutex || uxSemaphoreGetCount(sAudioMutex) == 0;
}

bool speakerWaitOthersReleased(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (sFireworksActive && millis() - t0 < timeoutMs) delay(50);
  return !sFireworksActive;
}
