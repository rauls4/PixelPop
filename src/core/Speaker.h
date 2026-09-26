#pragma once
// The onboard speaker path shared by sound-producing modules:
//   ESP32-S3 --I2S--> ES8311 codec --> power amplifier --> speaker header
// plus the rule that only ONE module may use it at a time.
//
// The I2S pin numbers come from community sources for the Waveshare
// ESP32-S3-RGB-Matrix and are UNVERIFIED. If your board differs, change them here.
// (The I2C pins 47/48 are confirmed by Waveshare's sensor example.)

#include <Arduino.h>

#define AUDIO_PIN_MCLK 12
#define AUDIO_PIN_BCLK 43
#define AUDIO_PIN_WS   38
#define AUDIO_PIN_DOUT 21
#define AUDIO_PIN_PA   11          // power-amplifier enable (HIGH = on)
#define AUDIO_PIN_SDA  47          // codec I2C (the code also tries the two swapped)
#define AUDIO_PIN_SCL  48

// ---- codec (call after the I2S clocks are running) ----
// Finds and configures the ES8311, then (amp = true) switches the amplifier on.
bool speakerCodecStart(uint32_t sampleRate, bool amp, char *msg, size_t msgLen);
void speakerCodecStop();           // mute, amplifier off, release the I2C bus
bool speakerCodecOpen();           // true while the codec is set up (and so the I2C bus is running)

void speakerAmp(bool on);          // switch just the power amplifier (quiet between sounds)
bool speakerAudioTake(unsigned long waitMs);  // reserve the shared I2S peripheral
void speakerAudioGive();
bool speakerAudioIsBusy();                    // true when another sound owns I2S

// ---- who has the speaker ----
// Priority: urgent notice first, then chime, then fireworks. The chime asks fireworks to
// let go, plays, then lets them resume.
void speakerFireworksActive(bool on);      // fireworks: "I am using the speaker"
bool speakerFireworksIsPlaying();
void speakerChimeRequest(bool on);         // chime: "please leave the speaker to me"
bool speakerChimeWaiting();
bool speakerWaitOthersReleased(uint32_t timeoutMs);  // chime: wait until fireworks let go

// The urgent notice outranks everything, the chime included: fireworks step aside
// (they see it through speakerChimeWaiting), and the chime stops playing and stays quiet.
void speakerUrgent(bool on);
bool speakerUrgentIsOn();
bool speakerChimeBusy();                   // the chime's own request is still up (it is playing or about to)
