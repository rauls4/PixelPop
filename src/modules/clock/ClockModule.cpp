#include "ClockModule.h"
#include <math.h>
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../../core/AudioOut.h"
#include "../../core/Speaker.h"

static const uint32_t DEF_COL_TIME   = 0xFFFFFF;   // clock digits
static const uint32_t DEF_COL_ACCENT = 0x00C8FF;   // date, AM/PM, seconds bar

static const char *WDAY[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void ClockModule::onLoad() {
  _showDate  = store.getBool("date", true);
  _showBar   = store.getBool("bar", true);
  _pendulumSound = store.getBool("pend", false);
  _colTime   = store.getUInt("cTime", DEF_COL_TIME);
  _colAccent = store.getUInt("cAcc", DEF_COL_ACCENT);
}

String ClockModule::onSettingsHtml() {
  String h;
  h += uiSection("Format");
  h += uiCheckbox("date", "Show the date (and AM/PM)", _showDate);
  h += uiCheckbox("bar", "Show the seconds bar along the bottom", _showBar);
  h += uiCheckbox("pend", "Play Grandfather pendulum tick-tock in portrait", _pendulumSound);
  h += "<p class='m'>The sound is quiet and only plays while the portrait Grandfather clock is visible. It yields to chimes, alerts, games, and fireworks.</p>";
  h += "<p class='m'>Time zone and 24-hour time are set on the Home page under General.</p>";

  h += uiSection("Colors");
  h += uiColor("cTime", "Clock digits", _colTime);
  h += uiColor("cAcc", "Date, AM/PM and seconds bar", _colAccent);
  return h;
}

void ClockModule::onSave(WebServer &server) {
  _showDate = server.hasArg("date");
  _showBar  = server.hasArg("bar");
  _pendulumSound = server.hasArg("pend");
  uint32_t c;
  if (uiReadColor(server, "cTime", c)) _colTime = c;
  if (uiReadColor(server, "cAcc", c))  _colAccent = c;
  store.putBool("date", _showDate);
  store.putBool("bar", _showBar);
  store.putBool("pend", _pendulumSound);
  store.putUInt("cTime", _colTime);
  store.putUInt("cAcc", _colAccent);
}

String ClockModule::actionsHtml() {
  return "<form method='POST' action='/clock/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void ClockModule::registerRoutes(WebServer &server) {
  server.on("/clock/colors-reset", HTTP_POST, [this]() {
    _colTime = DEF_COL_TIME;
    _colAccent = DEF_COL_ACCENT;
    store.putUInt("cTime", _colTime);
    store.putUInt("cAcc", _colAccent);
    app.requestRedraw();
    webRedirect("/clock?saved=1");
  });
}

String ClockModule::summary() {
  struct tm t;
  if (!timeNow(t)) return "Waiting for the time...";
  char buf[24];
  int h = t.tm_hour;
  if (app.use24Hour()) {
    snprintf(buf, sizeof(buf), "%02d:%02d", h, t.tm_min);
  } else {
    bool pm = h >= 12;
    h %= 12;
    if (h == 0) h = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h, t.tm_min, pm ? "PM" : "AM");
  }
  return String(buf);
}

bool ClockModule::pageAvailable(int sub) {
  (void)sub;
  struct tm t;
  return timeNow(t);
}

// The clock redraws once per second (colon blink + seconds bar).
bool ClockModule::needsRedraw() {
  struct tm t;
  if (!timeNow(t)) return false;
  if (gDisplay->height() > gDisplay->width()) return millis() - _lastFrame >= 40;
  return t.tm_sec != _lastSec;
}

void ClockModule::drawPortrait(const struct tm &t) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const int cx = W / 2;
  const int caseX = 1, caseY = 1, caseW = W - 2, caseH = H - 2;
  const int faceY = 15, faceR = (W - 8) / 2;
  const uint16_t wood = rgb565(0x5D2E18);
  const uint16_t woodLight = rgb565(0x9A552A);
  const uint16_t face = rgb565(0xF4E4C1);
  const uint16_t hand = rgb565(0x000000);
  const uint16_t brass = rgb565(0xE7B43A);

  gDisplay->fillScreen(0);
  gDisplay->fillRoundRect(caseX, caseY, caseW, caseH, 4, wood);
  gDisplay->drawRoundRect(caseX, caseY, caseW, caseH, 4, woodLight);
  gDisplay->fillRoundRect(4, 28, W - 8, H - 34, 2, rgb565(0x140A05));

  gDisplay->fillCircle(cx, faceY, faceR + 1, woodLight);
  gDisplay->fillCircle(cx, faceY, faceR, face);
  for (int i = 0; i < 12; i++) {
    const float a = (float)i * (2.0f * (float)M_PI / 12.0f) - (float)M_PI / 2.0f;
    const int x0 = cx + (int)lroundf(cosf(a) * (faceR - 1));
    const int y0 = faceY + (int)lroundf(sinf(a) * (faceR - 1));
    const int x1 = cx + (int)lroundf(cosf(a) * (faceR - 3));
    const int y1 = faceY + (int)lroundf(sinf(a) * (faceR - 3));
    gDisplay->drawLine(x0, y0, x1, y1, wood);
  }

  const float minute = (float)t.tm_min + (float)t.tm_sec / 60.0f;
  const float hour = (float)(t.tm_hour % 12) + minute / 60.0f;
  const float minuteA = minute * (2.0f * (float)M_PI / 60.0f) - (float)M_PI / 2.0f;
  const float hourA = hour * (2.0f * (float)M_PI / 12.0f) - (float)M_PI / 2.0f;
  gDisplay->drawLine(cx, faceY, cx + (int)lroundf(cosf(hourA) * (faceR * 0.48f)), faceY + (int)lroundf(sinf(hourA) * (faceR * 0.48f)), hand);
  gDisplay->drawLine(cx, faceY, cx + (int)lroundf(cosf(minuteA) * (faceR * 0.72f)), faceY + (int)lroundf(sinf(minuteA) * (faceR * 0.72f)), hand);
  gDisplay->fillCircle(cx, faceY, 1, brass);

  const int pivotY = 31, rodLength = H - 47;
  const float swing = sinf(millis() * 0.006f) * 0.42f;
  const int bobX = cx + (int)lroundf(sinf(swing) * rodLength);
  const int bobY = pivotY + (int)lroundf(cosf(swing) * rodLength);
  gDisplay->drawLine(cx, pivotY, bobX, bobY, brass);
  gDisplay->fillCircle(bobX, bobY, 4, brass);
  gDisplay->drawCircle(bobX, bobY, 4, woodLight);

  int displayHour = t.tm_hour;
  if (!app.use24Hour()) {
    displayHour %= 12;
    if (displayHour == 0) displayHour = 12;
  }
  const char *meridiem = app.use24Hour() ? "" : (t.tm_hour >= 12 ? "PM" : "AM");
  char time[9];
  snprintf(time, sizeof(time), app.use24Hour() ? "%02d:%02d" : "%d:%02d%s", displayHour, t.tm_min, meridiem);
  gDisplay->setFont(&TomThumb);
  int16_t textX, textY;
  uint16_t textW, textH;
  gDisplay->getTextBounds(time, 0, 0, &textX, &textY, &textW, &textH);
  const int digitalH = 10;
  const int digitalW = min(W - 4, (int)textW + 8);
  const int digitalX = (W - digitalW) / 2;
  const int digitalY = H - digitalH - 1;
  gDisplay->fillRoundRect(digitalX, digitalY, digitalW, digitalH, 3, face);
  gDisplay->drawRoundRect(digitalX, digitalY, digitalW, digitalH, 3, woodLight);
  gDisplay->setTextColor(wood);
  gDisplay->setCursor(cx - (int)textW / 2 - textX,
                      digitalY + (digitalH - (int)textH) / 2 - textY);
  gDisplay->print(time);
  gDisplay->setFont(nullptr);
  gDisplay->flipDMABuffer();
}

void ClockModule::startPendulumSound(bool high) {
  if (_pendulumSoundRunning || speakerAudioIsBusy() ||
      speakerFireworksIsPlaying() || speakerChimeBusy() || speakerUrgentIsOn()) return;
  _pendulumSoundRunning = true;
  _pendulumHigh = high;
  if (xTaskCreatePinnedToCore(pendulumTaskEntry, "clock-tick", 3072, this, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    _pendulumSoundRunning = false;
  }
}

void ClockModule::pendulumTaskEntry(void *arg) {
  ((ClockModule *)arg)->playPendulumSound();
  vTaskDelete(nullptr);
}

void ClockModule::playPendulumSound() {
  AudioOut audio;
  AudioConfig config = {true, 0, 0, 0};
  char status[48];
  if (!audio.open(config, true, status, sizeof(status))) {
    _pendulumSoundRunning = false;
    return;
  }
  int16_t buffer[128 * 2];
  const float frequency = _pendulumHigh ? 310.0f : 205.0f;
  for (int chunk = 0; chunk < 10; chunk++) {
    for (int i = 0; i < 128; i++) {
      const float time = (chunk * 128 + i) / (float)AUDIO_SAMPLE_RATE;
      const float envelope = 0.10f * expf(-time * 35.0f);
      const float sample = sinf(time * frequency * 6.2831853f) * envelope;
      const int16_t value = (int16_t)(sample * 32767.0f);
      buffer[i * 2] = buffer[i * 2 + 1] = value;
    }
    audio.write(buffer, 128);
  }
  audio.close();
  _pendulumSoundRunning = false;
}

void ClockModule::drawPage(int sub) {
  (void)sub;
  struct tm t;
  if (!timeNow(t)) return;

  if (gDisplay->height() > gDisplay->width()) {
    drawPortrait(t);
    if (_pendulumSound && t.tm_sec != _lastPendulumSec) {
      _lastPendulumSec = t.tm_sec;
      startPendulumSound((t.tm_sec & 1) == 0);
    }
    _lastSec = t.tm_sec;
    _lastFrame = millis();
    return;
  }

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);

  int hour = t.tm_hour;
  bool pm = hour >= 12;
  if (!app.use24Hour()) {
    hour = hour % 12;
    if (hour == 0) hour = 12;
  }
  bool colon = (t.tm_sec % 2 == 0);

  char hh[4], mm[4];
  if (app.use24Hour()) snprintf(hh, sizeof(hh), "%02d", hour);
  else         snprintf(hh, sizeof(hh), "%d", hour);
  snprintf(mm, sizeof(mm), "%02d", t.tm_min);

  const int W = gDisplay->width();

  // A Helvetica-like font reads much better blown up than the classic bitmap font did; the
  // colon is always printed (never swapped for a space) so its advance width never changes --
  // only its color toggles between visible and background -- so the digits never shift on blink.
  char tbuf[12];
  snprintf(tbuf, sizeof(tbuf), "%s:%s", hh, mm);
  gDisplay->setTextSize(1);
  gDisplay->setFont(&FreeSansBold9pt7b);
  int16_t tx, ttop;
  uint16_t tw, th;
  gDisplay->getTextBounds(tbuf, 0, 0, &tx, &ttop, &tw, &th);
  const int top = 3;                 // a pixel lower than before
  const int ty = top - ttop;
  gDisplay->setCursor((W - (int)tw) / 2 - tx, ty);
  gDisplay->setTextColor(rgb565(_colTime));
  gDisplay->print(hh);
  gDisplay->setTextColor(colon ? rgb565(_colTime) : 0);
  gDisplay->print(":");
  gDisplay->setTextColor(rgb565(_colTime));
  gDisplay->print(mm);
  gDisplay->antialiasText((W - (int)tw) / 2, top, (int)tw, (int)th, rgb565(_colTime));
  gDisplay->setFont(nullptr);

  // Date and AM/PM on one line, centered together
  gDisplay->setTextSize(1);
  gDisplay->setTextColor(rgb565(_colAccent));
  char d[16] = "";
  if (_showDate) snprintf(d, sizeof(d), "%s %d/%d", WDAY[t.tm_wday % 7], t.tm_mon + 1, t.tm_mday);
  const char *ap = app.use24Hour() ? "" : (pm ? "PM" : "AM");
  int dw = (int)strlen(d) * 6 - (d[0] ? 1 : 0);
  int aw = (int)strlen(ap) * 6 - (ap[0] ? 1 : 0);
  int gap = (d[0] && ap[0]) ? 6 : 0;
  if (dw + gap + aw > W - 2 && ap[0]) {              // too long: single-letter A / P
    ap = pm ? "P" : "A";
    aw = 5;
    gap = d[0] ? 4 : 0;
  }
  int x = (W - (dw + gap + aw)) / 2;
  if (x < 0) x = 0;
  if (d[0]) {
    gDisplay->setCursor(x, 21);
    gDisplay->print(d);
  }
  if (ap[0]) {
    gDisplay->setCursor(x + dw + gap, 21);
    gDisplay->print(ap);
  }

  // Seconds bar along the bottom row
  if (_showBar) {
    int barW = (t.tm_sec * gDisplay->width()) / 59;
    gDisplay->drawFastHLine(0, gDisplay->height() - 1, barW + 1, rgb565(_colAccent));
  }

  gDisplay->flipDMABuffer();
  _lastSec = t.tm_sec;
}
