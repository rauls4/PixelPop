#include "App.h"
#include "Alert.h"
#include "Spinner.h"
#include <WiFi.h>
#include <esp_system.h>
#include "CrashLog.h"
#include "Config.h"
#include "Settings.h"
#include "Display.h"
#include "SplashLogos.h"
#include "Orientation.h"
#include "StartupSound.h"
#include "TimeService.h"
#include "Net.h"
#include "WebUI.h"
#include "Transition.h"
#include "Sleep.h"
#include "NetLock.h"
#include <Fonts/TomThumb.h>

App app;

void App::add(Module *m) {
  if (_n < MAX_MODULES) _mods[_n++] = m;
}

Module *App::find(const char *id) {
  for (int i = 0; i < _n; i++) {
    if (strcmp(_mods[i]->id(), id) == 0) return _mods[i];
  }
  return nullptr;
}

// ---------- order of the modules ----------
String App::orderCsv() const {
  String s;
  for (int i = 0; i < _n; i++) {
    if (i) s += ",";
    s += _mods[i]->id();
  }
  return s;
}

// Put the modules in the order listed (ids separated by commas). Modules that are not listed
// keep their place after the listed ones, so a newly added module never disappears.
void App::applyOrder(const String &csv, bool keepScreen) {
  Module *cur = nullptr;
  int curSub = 0;
  if (keepScreen) slotInfo(_slot, cur, curSub);            // what is on the panel now

  Module *out[MAX_MODULES];
  bool used[MAX_MODULES] = {false};
  int k = 0;
  int start = 0;
  while (start <= (int)csv.length()) {
    int comma = csv.indexOf(',', start);
    if (comma < 0) comma = csv.length();
    String id = csv.substring(start, comma);
    id.trim();
    for (int i = 0; i < _n; i++) {
      if (!used[i] && id == _mods[i]->id()) { used[i] = true; out[k++] = _mods[i]; break; }
    }
    start = comma + 1;
  }
  for (int i = 0; i < _n; i++) if (!used[i]) out[k++] = _mods[i];
  for (int i = 0; i < _n; i++) _mods[i] = out[i];

  if (cur) {                                                // keep showing the same page
    int c = 0;
    for (int i = 0; i < _n; i++) {
      if (!_mods[i]->hasPages()) continue;
      int pc = _mods[i]->pageCount();
      if (pc <= 0) continue;
      if (_mods[i] == cur) { _slot = c + (curSub < pc ? curSub : 0); break; }
      c += pc;
    }
  }
}

bool App::setOrder(const String &csv) {
  String before = orderCsv();
  applyOrder(csv, true);
  String after = orderCsv();
  Store g("");
  g.putString("order", after);
  _slotSince = millis();
  return strcmp(before.c_str(), after.c_str()) != 0;
}

// ---------- general settings ----------
void App::setGeneral(uint8_t brightness, uint8_t tz, uint8_t padding, uint8_t transition, uint8_t transitionSpeed, uint8_t colorOrder, bool use24Hour) {
  if (brightness < 5) brightness = 5;
  if (brightness > MAX_BRIGHTNESS) brightness = MAX_BRIGHTNESS;
  if (tz >= timeZoneCount()) tz = 3;
  if (padding > MAX_PADDING) padding = MAX_PADDING;
  if (transition >= transitionCount()) transition = 0;
  if (transitionSpeed > 2) transitionSpeed = 1;
  if (colorOrder >= COLOR_ORDERS) colorOrder = 0;
  _brightness = brightness;
  _tz = tz;
  _pad = padding;
  _trStyle = transition;
  _trSpeed = transitionSpeed;
  _colorOrder = colorOrder;
  _use24Hour = use24Hour;
  Store g("");
  g.putU8("bright", _brightness);
  g.putU8("tz", _tz);
  g.putU8("pad", _pad);
  g.putU8("trn", _trStyle);
  g.putU8("trs", _trSpeed);
  g.putU8("cord", _colorOrder);
  g.putBool("time24", _use24Hour);
  if (!sleepIsAsleep()) displaySetBrightness(_brightness);      // a sleeping display keeps its own (dark) level
  displaySetPadding(_pad);
  displaySetColorOrder(_colorOrder);
  timeSetZone(_tz);
  _needRedraw = true;
}

void App::setRotation(uint8_t rotation) {
  rotation &= 3;
  Store g("");
  g.putU8("rot", rotation);
  g.putBool("rotV2", true);
  g.putBool("rotV3", true);
  applyRotation(rotation);
}

void App::applyRotation(uint8_t rotation) {
  rotation &= 3;
  if (_rotation == rotation) return;
  Module *active = nullptr;
  int activeSub = 0;
  const bool keepActive = slotInfo(_slot, active, activeSub);
  _rotation = rotation;
  displaySetRotation(rotation);
  if (keepActive && active) {
    int first = 0;
    for (int i = 0; i < _n; i++) {
      Module *candidate = _mods[i];
      if (!candidate->hasPages()) continue;
      const int pages = candidate->pageCount();
      if (candidate == active) {
        if (pages > 0) _slot = first + min(activeSub, pages - 1);
        break;
      }
      if (pages > 0) first += pages;
    }
  }
  _needRedraw = true;
}

bool App::imuAvailable() const { return orientationAvailable(); }
uint8_t App::imuAddress() const { return orientationAddress(); }
OrientationSample App::imuSample() const { return orientationSample(); }

void App::setAutoOrientation(bool on) {
  _autoOrientation = on;
  Store g("");
  g.putBool("autoOri", _autoOrientation);
  if (_autoOrientation && !orientationAvailable()) orientationBegin();
  if (_autoOrientation) {
    uint8_t rotation = 0;
    if (orientationCurrent(rotation)) applyRotation(rotation);
  }
}

// ---------- color calibration (Advanced > Color Lab) ----------
// The Color Lab page takes the whole panel over for live tuning, the same way the urgent-notice
// overlay does, but driven by the web page instead of a module: App::loop() skips the normal
// rotation entirely while _calMode is set (see below), and the page itself draws every swatch.
static const unsigned long CAL_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;    // safety net if the page is left open

void App::calStart() {
  _calMode = true;
  _calTouch = millis();
  displaySetColorOrder(_colorOrder);
  displaySetGain(_gainR, _gainG, _gainB);
  drawColorSwatch(-1);
}

void App::calPreview(uint8_t order, uint8_t gainR, uint8_t gainG, uint8_t gainB, int swatch) {
  if (!_calMode) return;
  _calTouch = millis();
  if (order >= COLOR_ORDERS) order = _colorOrder;
  displaySetColorOrder(order);
  displaySetGain(gainR, gainG, gainB);
  drawColorSwatch(swatch);
}

void App::calSave(uint8_t order, uint8_t gainR, uint8_t gainG, uint8_t gainB) {
  if (order >= COLOR_ORDERS) order = _colorOrder;
  _colorOrder = order;
  _gainR = gainR; _gainG = gainG; _gainB = gainB;
  Store g("");
  g.putU8("cord", _colorOrder);
  g.putU8("gR", _gainR);
  g.putU8("gG", _gainG);
  g.putU8("gB", _gainB);
}

void App::calExit() {
  if (!_calMode) return;
  _calMode = false;
  displaySetColorOrder(_colorOrder);            // back to whatever was last saved (unsaved live tweaks are dropped)
  displaySetGain(_gainR, _gainG, _gainB);
  _needRedraw = true;
  _reenter = true;
  _slotSince = millis();
  _ipUntil = 0;
}

void App::refreshAll() {
  for (int i = 0; i < _n; i++) _mods[i]->refreshNow();
}

void App::resetWifi() {
  netRequestPortalOnNextBoot();
  delay(300);
  ESP.restart();
}

// ---------- IP address screen ----------
static void drawTinyMarquee(const String &text, int baseline, uint16_t color, unsigned long phase, unsigned long shownAt, int x0 = 0) {
  const int W = gDisplay->width();
  const int subW = W - x0;
  int16_t textX, textY;
  uint16_t textW, textH;
  gDisplay->getTextBounds(text.c_str(), 0, baseline, &textX, &textY, &textW, &textH);
  int x = x0 + (subW - (int)textW) / 2 - textX;
  if ((int)textW > subW - 2) {
    const int period = textW + 8;
    const unsigned long elapsed = millis() - shownAt;
    if (elapsed >= 1000) x = x0 - (int)(((elapsed - 1000) / 70 + phase) % period);
  }
  gDisplay->setClipX(x0, W);
  gDisplay->setTextColor(color);
  gDisplay->setCursor(x, baseline);
  gDisplay->print(text);
  if ((int)textW > subW - 2) {
    gDisplay->setCursor(x + (int)textW + 8, baseline);
    gDisplay->print(text);
  }
  gDisplay->clearClip();
}

static void drawWifiConnecting() {
  const int W = gDisplay->width(), H = gDisplay->height();
  gDisplay->fillScreen(0);
  gDisplay->drawBitmap((W - 32) / 2, (H - 25) / 2, WIFI_CONNECTING_LOGO, 32, 25, COL_WHITE);
  gDisplay->flipDMABuffer();
}

void App::drawIpScreen() {
  const int W = gDisplay->width(), H = gDisplay->height();
  if (H <= W) {
    gDisplay->fillScreen(0);
    gDisplay->setTextWrap(false);
    gDisplay->setFont(&TomThumb);
    const uint16_t label = COL_ORANGE;
    const uint16_t value = COL_WHITE;
    const int valueX = 21;                             // past the widest label ("LOCAL")
    gDisplay->setTextColor(label);
    gDisplay->setCursor(1, 9);  gDisplay->print("WIFI");
    drawTinyMarquee(WiFi.SSID(), 9, value, 0, _ipShownAt, valueX);
    gDisplay->setTextColor(label);
    gDisplay->setCursor(1, 19); gDisplay->print("IP");
    drawTinyMarquee(WiFi.localIP().toString(), 19, value, 17, _ipShownAt, valueX);
    gDisplay->setTextColor(label);
    gDisplay->setCursor(1, 29); gDisplay->print("LOCAL");
    drawTinyMarquee(netHostname() + ".local", 29, value, 31, _ipShownAt, valueX);
    gDisplay->setFont(nullptr);
    gDisplay->flipDMABuffer();
    return;
  }

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setFont(&TomThumb);
  const uint16_t label = COL_ORANGE;
  const uint16_t value = COL_WHITE;
  gDisplay->setTextColor(label);
  gDisplay->setCursor(1, 6);  gDisplay->print("WIFI");
  drawTinyMarquee(WiFi.SSID(), 15, value, 0, _ipShownAt);
  gDisplay->setTextColor(label);
  gDisplay->setCursor(1, 26); gDisplay->print("IP");
  drawTinyMarquee(WiFi.localIP().toString(), 35, value, 17, _ipShownAt);
  gDisplay->setTextColor(label);
  gDisplay->setCursor(1, 46); gDisplay->print("LOCAL");
  drawTinyMarquee(netHostname() + ".local", 55, value, 31, _ipShownAt);
  gDisplay->setFont(nullptr);
  gDisplay->flipDMABuffer();
}

void App::showIp() {
  _showingIpScreen = true;
  _ipShownAt = millis();
  _lastIpFrame = 0;
  drawIpScreen();
  _ipUntil = millis() + IP_SHOW_MS;
  Serial.print("Settings page: http://");
  Serial.println(WiFi.localIP());
}

void App::showAccessCode(uint32_t code, unsigned long ms) {
  char text[5];
  snprintf(text, sizeof(text), "%04lu", (unsigned long)(code % 10000));
  const int W = gDisplay->width(), H = gDisplay->height();
  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setTextSize(1);
  gDisplay->setTextColor(COL_ORANGE);
  const char *title = "CODE";
  gDisplay->setCursor((W - (int)strlen(title) * 6) / 2, H / 2 - 10);
  gDisplay->print(title);
  const int size = W >= 48 ? 2 : 1;
  gDisplay->setTextSize(size);
  gDisplay->setTextColor(COL_WHITE);
  gDisplay->setCursor((W - ((int)strlen(text) * 6 * size - size)) / 2, H / 2 + 2);
  gDisplay->print(text);
  gDisplay->flipDMABuffer();
  _showingIpScreen = false;
  _ipUntil = millis() + ms;
}

static void drawPortraitMessage(const char *l1, const char *l2, const char *l3) {
  const int W = gDisplay->width(), H = gDisplay->height();
  const char *const lines[] = {l1, l2, l3};
  int count = l3 ? 3 : (l2 ? 2 : 1);
  const int firstBaseline = H / 2 - (count - 1) * 5 + 2;

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(COL_ORANGE);
  for (int i = 0; i < count; i++) {
    const int textW = (int)strlen(lines[i]) * 4 - 1;
    gDisplay->setClipX(0, W);
    gDisplay->setCursor((W - textW) / 2, firstBaseline + i * 10);
    gDisplay->print(lines[i]);
  }
  gDisplay->clearClip();
  gDisplay->setFont(nullptr);
  gDisplay->flipDMABuffer();
}

// A short message over whatever is showing (like the IP address, the rotation pauses while it is up).
void App::showMessage(const char *l1, const char *l2, const char *l3, unsigned long ms) {
  _showingIpScreen = false;
  if (gDisplay->height() > gDisplay->width()) drawPortraitMessage(l1, l2, l3);
  else drawLines(l1, l2, l3);
  _ipUntil = millis() + ms;
}

void App::previewModule(const char *id) {
  if (!id || !id[0]) return;
  strncpy(_previewId, id, sizeof(_previewId) - 1);
  _previewId[sizeof(_previewId) - 1] = 0;
  _previewUntil = millis() + PREVIEW_HOLD_MS;
}

void App::showFirmwareProgress(uint8_t percent) {
  _showingIpScreen = false;
  if (percent > 100) percent = 100;
  const int W = gDisplay->width(), H = gDisplay->height();
  const bool portrait = H > W;
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(1);
  gDisplay->setTextWrap(false);

  char text[8];
  snprintf(text, sizeof(text), "%u%%", (unsigned)percent);

  // Landscape with room to spare: the PixelPop logo takes the place of the "UPDATING" title,
  // and the bar alone (no percent text) fills the space below it.
  if (!portrait && H >= 30) {
    const int logoW = 64, logoH = 22;
    gDisplay->drawBitmap((W - logoW) / 2, 0, SPLASH_LOGO_LANDSCAPE, logoW, logoH, COL_WHITE);
    const int barX = 3, barY = logoH + 1, barW = W - 6, barH = H - barY - 1;
    gDisplay->drawRect(barX, barY, barW, barH, COL_GRAY);
    const int filled = (barW - 2) * percent / 100;
    if (filled > 0) gDisplay->fillRect(barX + 1, barY + 1, filled, barH - 2, COL_ORANGE);
    gDisplay->flipDMABuffer();
    _ipUntil = millis() + 600000;
    return;
  }

  const int barX = 3, barY = H - 8, barW = W - 6, barH = 6;
  gDisplay->setTextColor(COL_ORANGE);
  if (portrait) {
    const char *title = "UPDATING FIRMWARE";
    const int titleW = (int)strlen(title) * 4 - 1;
    const int gap = 10;
    const int offset = (int)((millis() / 45) % (unsigned long)(titleW + gap));
    gDisplay->setFont(&TomThumb);
    gDisplay->setClipX(0, W);
    gDisplay->setCursor(W - offset, 6);
    gDisplay->print(title);
    gDisplay->setCursor(W - offset + titleW + gap, 6);
    gDisplay->print(title);
    gDisplay->clearClip();
    gDisplay->setFont(nullptr);
  } else {
    const char *title = "UPDATING";
    gDisplay->setCursor((W - (int)strlen(title) * 6) / 2, 2);
    gDisplay->print(title);
  }
  gDisplay->setTextColor(COL_WHITE);
  gDisplay->setCursor((W - (int)strlen(text) * 6) / 2, 12);
  gDisplay->print(text);

  // Portrait has plenty of headroom below the title/percent for the logo too.
  if (portrait) {
    const int logoW = 32, logoH = 28;
    const int gapTop = 19, gapBottom = barY;              // free space between percent text and the bar
    if (gapBottom - gapTop >= logoH + 4) {
      const int logoY = gapTop + (gapBottom - gapTop - logoH) / 2;
      gDisplay->drawBitmap((W - logoW) / 2, logoY, SPLASH_LOGO_PORTRAIT, logoW, logoH, COL_WHITE);
    }
  }

  gDisplay->drawRect(barX, barY, barW, barH, COL_GRAY);
  const int filled = (barW - 2) * percent / 100;
  if (filled > 0) gDisplay->fillRect(barX + 1, barY + 1, filled, barH - 2, COL_ORANGE);
  gDisplay->flipDMABuffer();
  _ipUntil = millis() + 600000;
}

// ---------- startup ----------
void App::begin() {
  Serial.begin(115200);
  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("Reset reason: %s (%d)\n", crashLogReasonName(resetReason), resetReason);
  crashLogRecord(resetReason);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // General settings (same flash keys as earlier versions: "bright", "tz")
  Store g("");
  _brightness = g.getU8("bright", 100);
  if (_brightness < 5) _brightness = 5;
  if (_brightness > MAX_BRIGHTNESS) _brightness = MAX_BRIGHTNESS;      // older versions allowed up to 255
  _tz = g.getU8("tz", 3);
  if (_tz >= timeZoneCount()) _tz = 3;
  _pad = g.getU8("pad", 1);                 // default: 1 pixel of padding
  if (_pad > MAX_PADDING) _pad = MAX_PADDING;
  _trStyle = g.getU8("trn", 3);              // default: slide left
  if (_trStyle >= transitionCount()) _trStyle = 3;
  _trSpeed = g.getU8("trs", 1);
  if (_trSpeed > 2) _trSpeed = 1;
  _colorOrder = g.getU8("cord", 1);          // default: swap green and blue
  if (_colorOrder >= COLOR_ORDERS) _colorOrder = 1;
  _gainR = g.getU8("gR", 255);
  _gainG = g.getU8("gG", 255);
  _gainB = g.getU8("gB", 255);
  _use24Hour = g.getBool("time24", false);
  const uint8_t storedRotation = g.getU8("rot", 0xFF);
  if (storedRotation == 0xFF) {
    _rotation = 0;
  } else {
    _rotation = storedRotation & 3;
    if (!g.getBool("rotV2", false)) {                   // migrate the previous landscape-first mapping
      _rotation = (_rotation + 3) & 3;
      g.putU8("rot", _rotation);
      g.putBool("rotV2", true);
    }
    if (!g.getBool("rotV3", false)) {                   // migrate the earlier portrait-first transform
      _rotation = (4 - _rotation) & 3;
      g.putU8("rot", _rotation);
      g.putBool("rotV3", true);
    }
  }
  _autoOrientation = g.getBool("autoOri", true);

  applyOrder(g.getString("order", ""), false);        // the order chosen on the web page
  for (int i = 0; i < _n; i++) _mods[i]->load();
  sleepBegin();

  memMark("start");
  if (!gDisplay) displayBegin(_brightness, _pad);
  else {
    displaySetBrightness(_brightness);
    displaySetPadding(_pad);
  }
  displaySetRotation(_rotation);
  displaySetColorOrder(_colorOrder);
  displaySetGain(_gainR, _gainG, _gainB);
  orientationBegin();
  if (_autoOrientation) {
    uint8_t rotation = 0;
    if (orientationCurrent(rotation)) applyRotation(rotation);
  }
  memMark("display ready");
  displayShowSplash();

  // Wi-Fi: use saved credentials, or open the setup network
  netLoadCreds();
  if (netForcePortal() || !netHasCreds()) netRunPortal();          // never returns

  drawWifiConnecting();
  if (!netConnect(20000)) {
    drawLines("No WiFi", "Starting", "setup...");
    delay(1500);
    netRunPortal();                                               // never returns
  }

  memMark("wifi connected");
  timeSetZone(_tz);
  for (int i = 0; i < _n; i++) _mods[i]->begin();
  memMark("modules started");
  webBegin();
  memMark("web ready");

  // Show Wi-Fi and network details for 20 seconds (the web page is already live)
  showIp();
  unsigned long t0 = millis();
  while (millis() - t0 < IP_SHOW_MS) {
    webLoop();
    if (_showingIpScreen && millis() - _lastIpFrame >= 70) {
      drawIpScreen();
      _lastIpFrame = millis();
    }
    delay(5);
  }
  _ipUntil = 0;
  _showingIpScreen = false;
  _slotSince = millis();
  _lastWifiRetry = millis();
}

// ---------- page slots: every (module, sub-page) pair, in order ----------
int App::slotCount() {
  int n = 0;
  for (int i = 0; i < _n; i++) {
    if (!_mods[i]->hasPages()) continue;
    int pc = _mods[i]->pageCount();
    if (pc > 0) n += pc;
  }
  return n;
}

bool App::slotInfo(int idx, Module *&m, int &sub) {
  int c = 0;
  for (int i = 0; i < _n; i++) {
    if (!_mods[i]->hasPages()) continue;
    int pc = _mods[i]->pageCount();
    if (pc <= 0) continue;
    if (idx < c + pc) {
      m = _mods[i];
      sub = idx - c;
      return true;
    }
    c += pc;
  }
  return false;
}

bool App::slotAvailable(int idx) {
  Module *m;
  int sub;
  if (!slotInfo(idx, m, sub)) return false;
  return m->enabled() && m->pageAvailable(sub);
}

// ---------- BOOT button: tap = show IP, hold 5 s = change Wi-Fi ----------
void App::handleButton(unsigned long now) {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (_buttonDownAt == 0) _buttonDownAt = now;
    if (now - _buttonDownAt > 5000) {
      drawLines("Resetting", "WiFi...");
      delay(800);
      resetWifi();
    }
  } else {
    if (_buttonDownAt != 0 && now - _buttonDownAt > 30 && now - _buttonDownAt < 1500) {
      if (alertActive()) alertRequestDismiss();          // a tap silences an urgent notice
      else if (sleepIsAsleep()) sleepWake();             // ...or wakes a sleeping display
      else showIp();
    }
    _buttonDownAt = 0;
  }
}

// ---------- main loop ----------
void App::loop() {
  unsigned long now = millis();

  webLoop();
  handleButton(now);

  // Color Lab (Advanced > Color Lab) owns the whole panel while it is open: no rotation,
  // no sleep, just whatever swatch the page last asked for. Leaves by itself if the page
  // was left open and idle, so the panel never gets stuck on a test color.
  //
  // Bug fixed here: this used to compare against `now`, captured at the very top of loop()
  // BEFORE webLoop() runs. If the request that just called calStart() is handled inside that
  // same webLoop() call, _calTouch (set to millis() during that call) ends up LATER than the
  // already-captured `now` -- so `now - _calTouch` underflows (both are unsigned long) to a
  // huge number, blowing well past CAL_IDLE_TIMEOUT_MS and calling calExit() one loop iteration
  // after calStart(), before the page's first live update ever lands. Re-read the clock here
  // so the comparison is always against a timestamp taken after webLoop() has run.
  if (_calMode) {
    const unsigned long calNow = millis();
    if (calNow - _calTouch > CAL_IDLE_TIMEOUT_MS) calExit();
    else { delay(20); return; }
  }

  uint8_t imuRotation = 0;
  const bool imuChanged = orientationTick(imuRotation);
  if (_autoOrientation && imuChanged) applyRotation(imuRotation);
  const bool tapAdvance = orientationTap();

  // WiFi auto-reconnects after a drop. Do not reboot the whole app for a transient
  // router outage; periodically check its state without blocking the display loop.
  if (WiFi.status() != WL_CONNECTED && now - _lastWifiRetry >= WIFI_RETRY_MS) {
    _lastWifiRetry = now;
    netConnect(0);
  }

  // Background work for every module (fetching data, chimes, ...)
  for (int i = 0; i < _n; i++) _mods[i]->update();

  // ---- an urgent module (the notice) takes over the whole panel ----
  Module *over = nullptr;
  for (int i = 0; i < _n; i++) {
    if (_mods[i]->takesScreen()) { over = _mods[i]; break; }
  }
  // ---- sleeping: the panel stays dark (or shows the dim clock); the modules above keep fetching ----
  bool woke = false;
  if (sleepTick(over != nullptr, woke)) { delay(50); return; }
  if (woke) { _needRedraw = true; _slotSince = now; _ipUntil = 0; _reenter = true; }

  if (over) {
    if (!_overlayOn) { _overlayOn = true; _needRedraw = true; }
    if (_needRedraw || over->needsRedraw()) {
      over->drawPage(0);
      gDisplay->present();                                 // single-buffered: make sure the frame reaches the panel
      _needRedraw = false;
    }
    _showingMsg = false;
    _spinning = false;
    delay(20);
    return;
  }
  if (_overlayOn) {                                       // it is over: back to the rotation
    _overlayOn = false;
    _needRedraw = true;
    _reenter = true;
    _slotSince = now;
    _showingMsg = false;
    _ipUntil = 0;
  }

  // ---- settings-site hover preview: one module's page, live, instead of the normal rotation ----
  if (_previewId[0] && now < _previewUntil) {
    Module *pm = nullptr;
    for (int i = 0; i < _n; i++) if (strcmp(_mods[i]->id(), _previewId) == 0) { pm = _mods[i]; break; }
    if (pm) {
      if (!_previewActive) { pm->pageEntered(0); _previewActive = true; _needRedraw = true; }
      if (_needRedraw || pm->needsRedraw()) {
        pm->drawPage(0);
        gDisplay->present();                               // single-buffered: make sure the frame reaches the panel
        _needRedraw = false;
      }
      delay(20);
      return;
    }
    _previewId[0] = 0;                                     // unknown id: fall through and clear it below
  }
  if (_previewActive) {                                     // preview ended (expired, or the id vanished): back to the rotation
    _previewActive = false;
    _needRedraw = true;
    _reenter = true;
    _slotSince = now;
  }

  // ---- rotation ----
  int n = slotCount();
  bool haveSlot = false;
  Module *cm = nullptr;
  int sub = 0;
  if (n > 0) {
    if (_slot >= n) _slot = 0;
    bool avail = slotAvailable(_slot);
    slotInfo(_slot, cm, sub);
    unsigned long dwell = (cm ? cm->seconds() : 6) * 1000UL;

    const int progress = (avail && cm) ? cm->pageProgress() : 0;
    const bool due = tapAdvance || progress == 2 || (progress == 0 && now - _slotSince >= dwell);
    if (!avail || due) {
      bool moved = false;
      for (int i = 1; i <= n; i++) {
        int idx = (_slot + i) % n;
        if (slotAvailable(idx)) {
          if (idx != _slot) { _needRedraw = true; moved = true; }
          _slot = idx;
          _slotSince = now;
          break;
        }
      }
      if (!avail) _slotSince = now;
      slotInfo(_slot, cm, sub);
      haveSlot = slotAvailable(_slot);
      if (haveSlot && cm) { cm->pageEntered(sub); _reenter = false; }        // a fresh showing (before its transition draws it)

      // play the transition effect into the new page (not over the IP screen or a message)
      const int transitionStyle = cm ? cm->transitionOverride() : -1;
      const uint8_t effectiveTransition = transitionStyle >= 0 ? (uint8_t)transitionStyle : _trStyle;
      if (moved && haveSlot && cm && effectiveTransition != 0 && !_showingMsg && millis() >= _ipUntil) {
        transitionPlay(effectiveTransition, _trSpeed, cm, sub);
        _slotSince = millis();
        _needRedraw = true;
      }
    }
    haveSlot = slotAvailable(_slot);
    if (_reenter && haveSlot && cm) { cm->pageEntered(sub); _reenter = false; }   // back after sleep or a notice: start the page over
  }

  // ---- drawing (paused while the IP address is on screen) ----
  bool showingIp = millis() < _ipUntil;
  if (showingIp && _showingIpScreen && millis() - _lastIpFrame >= 70) {
    drawIpScreen();
    _lastIpFrame = millis();
  }
  if (!showingIp) _showingIpScreen = false;
  if (_wasShowingIp && !showingIp) _needRedraw = true;
  _wasShowingIp = showingIp;

  if (haveSlot && cm && cm->needsRedraw()) { _needRedraw = true; _fastUntil = millis() + 300; }
  if (!haveSlot && !_showingMsg) _needRedraw = true;    // switch to the message screen
  if (!haveSlot && _spinning && millis() - _spinAt >= 70) _needRedraw = true;    // next spinner frame

  if (_needRedraw && !showingIp) {
    if (haveSlot && cm) {
      cm->drawPage(sub);
      gDisplay->present();                                 // single-buffered: make sure the frame reaches the panel
      _showingMsg = false;
      _spinning = false;
    } else {
      bool anyOn = false;
      for (int i = 0; i < _n; i++) {
        if (_mods[i]->hasPages() && _mods[i]->enabled()) anyOn = true;
      }
      if (anyOn) { displaySpinner(millis()); _spinning = true; _spinAt = millis(); }     // waiting for data
      else       { drawLines("Nothing", "turned on"); _spinning = false; }
      _showingMsg = true;
    }
    _needRedraw = false;
  }

  // While a page animates (scrolling text, ...) come back within a millisecond, so a new frame goes out
  // the moment it is due; a longer pause here makes scrolling move in uneven steps.
  delay(millis() < _fastUntil ? 1 : 20);
}
