#include "Urgent.h"
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/App.h"
#include "../../core/Alert.h"
#include "../../core/AudioOut.h"
#include "../../core/Speaker.h"

static const int MSG_MAX = 100;
static const unsigned long FLASH_MS = 400;        // triangles and border flip color this often
static const unsigned long SCROLL_STEP = 20;      // ms per pixel of scrolling

static const uint32_t RED    = 0xFF2010;
static const uint32_t YELLOW = 0xFFD200;
static const uint32_t WHITE  = 0xFFFFFF;

// Printable ASCII only (the panel font has nothing else), trimmed.
static String cleanMsg(const String &in) {
  String out;
  for (size_t i = 0; i < in.length() && (int)out.length() < MSG_MAX; i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

// ---------- settings ----------
void UrgentModule::onLoad() {
  _msg     = cleanMsg(store.getString("msg", "TEST ALERT"));
  if (_msg.length() == 0) _msg = "TEST ALERT";
  long s   = (long)store.getUInt("dur", 30);
  _secs    = (uint16_t)(s < 5 ? 5 : (s > 300 ? 300 : s));
  long v   = (long)store.getU8("vol", 70);
  _volume  = (uint8_t)(v < 1 ? 1 : (v > 100 ? 100 : v));
  _sirenOn = store.getBool("sir", true);
}

String UrgentModule::onSettingsHtml() {
  String h;
  h += uiSection("When you send a notice");
  h += uiNumber("dur", "How long it stays on the display (seconds)", _secs, 5, 300);
  h += uiCheckbox("sir", "Sound the siren", _sirenOn);
  h += uiNumber("vol", "Siren volume (1 to 100)", _volume, 1, 100);
  h += "<p class='m'>Use the Send button below to fire a notice. It happens once, only when you press it. "
       "It ends by itself after this time, or sooner if you tap the BOOT button on the board or press Stop here.</p>";
  return h;
}

void UrgentModule::onSave(WebServer &server) {
  _secs    = (uint16_t)uiReadLong(server, "dur", _secs, 5, 300);
  _volume  = (uint8_t)uiReadLong(server, "vol", _volume, 1, 100);
  _sirenOn = server.hasArg("sir");
  store.putUInt("dur", _secs);
  store.putU8("vol", _volume);
  store.putBool("sir", _sirenOn);
}

// ---------- the web page ----------
String UrgentModule::summary() {
  if (_active) return _preview ? "Preview on the display" : "ALERT ON THE DISPLAY";
  String s = "Ready. Nothing is sent until you press Send";
  if (_sent) s += " (" + String((unsigned)_sent) + " sent since power-up)";
  return s;
}

String UrgentModule::actionsHtml() {
  String h = "<h3 class='first'>Send a notice</h3>";
  if (gServer.hasArg("sent")) {
    const String r = gServer.arg("sent");
    if (r == "1")      h += "<div class='ok'>The notice is on the display.</div>";
    else if (r == "off")  h += "<div class='err'>Urgent notices are turned off. Switch on \"Allow urgent notices\" above and save.</div>";
    else if (r == "busy") h += "<div class='err'>A notice is already on the display. Stop it first.</div>";
    else if (r == "empty") h += "<div class='err'>Type a message first.</div>";
  }
  if (_active) {
    h += "<div class='err'>A notice is on the display now.</div>"
         "<form method='POST' action='/urgent/stop'><button type='submit'>Stop it now</button></form>";
  }
  if (_sirenStatus[0]) h += "<p class='m'>Siren: " + htmlEscape(String(_sirenStatus)) + "</p>";
  h += "<form method='POST' action='/urgent/send' onsubmit=\"return this.p.value=='1'||confirm('Send the notice now? " +
       String(_sirenOn ? "The siren will sound." : "It will take over the display.") + "')\">"
       "<input type='hidden' name='p' value='0'>";
  h += uiText("msg", "Message (up to 100 characters)", _msg, MSG_MAX);
  h += "<p class='m'>Shown in capital letters. Long messages scroll.</p>"
       "<button type='submit' style='background:#d70015'>Send now</button>"
       "<button class='sec' type='submit' name='preview' value='1' onclick=\"this.form.p.value='1'\">Preview for 6 seconds (no siren)</button>"
       "</form>";
  return h;
}

void UrgentModule::registerRoutes(WebServer &server) {
  server.on("/urgent/send", HTTP_POST, [this, &server]() {
    const String m = cleanMsg(server.arg("msg"));
    const bool preview = server.hasArg("preview");
    if (!enabled()) { webRedirect("/urgent?sent=off"); return; }
    if (m.length() == 0) { webRedirect("/urgent?sent=empty"); return; }
    if (_active) { webRedirect("/urgent?sent=busy"); return; }
    _msg = m;
    store.putString("msg", _msg);
    trigger(_msg, preview);
    webRedirect("/urgent?sent=1");
  });
  server.on("/urgent/stop", HTTP_POST, [this]() {
    if (_active) alertRequestDismiss();
    webRedirect("/urgent");
  });
}

// ---------- firing ----------
bool UrgentModule::trigger(const String &msg, bool preview) {
  if (_active) return false;
  _shown = msg;
  _shown.toUpperCase();
  _preview = preview;
  _siren = _sirenOn && !preview && !_sirenBusy;
  const unsigned long ms = preview ? 6000UL : (unsigned long)_secs * 1000UL;
  _start = millis();
  _end = _start + ms;
  _lastFrame = 0;
  _sent++;
  _sirenStatus[0] = 0;
  if (_siren) {
    speakerUrgent(true);                               // fireworks step aside and the chime is silent
    _sirenBusy = true;
    if (xTaskCreatePinnedToCore(taskEntry, "siren", 8192, this, 2, nullptr, APP_TASK_CORE) != pdPASS) {
      _sirenBusy = false;
      speakerUrgent(false);
      snprintf(_sirenStatus, sizeof(_sirenStatus), "could not start the siren (not enough memory)");
    }
  }
  _active = true;
  alertSetActive(true);
  Serial.printf("Urgent notice: %s%s\n", _shown.c_str(), preview ? " (preview)" : "");
  return true;
}

void UrgentModule::finish() {
  _active = false;                                     // the siren task sees this and fades out
  alertSetActive(false);
  alertClearDismiss();
  Serial.println("Urgent notice over");
}

void UrgentModule::update() {
  if (!_active) return;
  if (alertDismissRequested() || (long)(millis() - _end) >= 0) finish();
}

// ---------- siren ----------
void UrgentModule::taskEntry(void *arg) {
  ((UrgentModule *)arg)->sirenTask();
  vTaskDelete(nullptr);
}

void UrgentModule::sirenTask() {
  // A chime that is playing stops by itself (it sees speakerUrgent); fireworks let go too.
  unsigned long t0 = millis();
  while (speakerChimeBusy() && millis() - t0 < 3000) delay(20);
  speakerWaitOthersReleased(6000);

  AudioOut audio;
  AudioConfig cfg;
  cfg.codec = true; cfg.bclk = 0; cfg.ws = 0; cfg.dout = 0;
  char msg[sizeof(_sirenStatus)];
  if (!audio.open(cfg, true, msg, sizeof(msg))) {
    snprintf(_sirenStatus, sizeof(_sirenStatus), "%s", msg);
    Serial.printf("Siren: %s\n", msg);
    speakerUrgent(false);
    _sirenBusy = false;
    return;
  }
  const float v = _volume / 100.0f;
  _synth.start(v * v);                                 // quadratic: feels more even to the ear
  static int16_t buf[256 * 2];
  while (_active) {
    _synth.render(buf, 256);
    audio.write(buf, 256);
  }
  _synth.stop();                                       // a short fade, so it does not click
  while (!_synth.finished()) {
    _synth.render(buf, 256);
    audio.write(buf, 256);
  }
  audio.close();
  speakerUrgent(false);                                // chimes may resume
  _sirenBusy = false;
}

// ---------- drawing ----------
// A warning triangle, 13 wide and 11 tall, with an exclamation mark.
static void drawTriangle(int x, int y, uint16_t fill, uint16_t mark) {
  const int cx = x + 6;
  for (int r = 0; r <= 10; r++) {
    const int half = (r * 6 + 5) / 10;
    gDisplay->drawFastHLine(cx - half, y + r, 2 * half + 1, fill);
  }
  gDisplay->drawFastVLine(cx, y + 3, 4, mark);
  gDisplay->drawPixel(cx, y + 8, mark);
}

// A new frame when the scrolling text has moved a pixel or the flashing has changed color (not on any
// other beat: that makes the text move one pixel, then two ...). Short messages do not scroll.
static unsigned long frameKey(unsigned long now, unsigned long start, size_t len) {
  const bool scrolls = (int)len * 12 - 2 > gDisplay->width() - 6;
  return (scrolls ? (now - start) / SCROLL_STEP : 0) * 2 + (((now - start) / FLASH_MS) & 1);
}

bool UrgentModule::needsRedraw() {
  return _active && frameKey(millis(), _start, _shown.length()) != _lastFrame;
}

void UrgentModule::drawPage(int sub) {
  (void)sub;
  const int W = gDisplay->width(), H = gDisplay->height();
  const unsigned long now = millis();
  _lastFrame = frameKey(now, _start, _shown.length());
  const bool phase = ((now - _start) / FLASH_MS) & 1;

  const uint16_t cRed = rgb565(RED), cYel = rgb565(YELLOW), cWhite = rgb565(WHITE), cBlack = 0;
  const uint16_t edge   = phase ? cYel : cRed;
  const uint16_t triA   = phase ? cRed : cYel;
  const uint16_t markA  = phase ? cWhite : cBlack;

  gDisplay->fillScreen(0);
  gDisplay->setTextWrap(false);

  // the message, size 2 (12 px per character), in the band under the header
  const int len = (int)_shown.length();
  const int tw = len * 12 - 2;
  const int y = 14;
  gDisplay->setTextSize(2);
  gDisplay->setTextColor(cWhite);
  if (tw <= W - 6) {
    gDisplay->setCursor((W - tw) / 2, y);
    gDisplay->print(_shown);
  } else {
    const int gap = 36;
    const int period = tw + gap;
    const int off = (int)(((now - _start) / SCROLL_STEP) % (unsigned long)period);
    for (int copy = 0; copy < 2; copy++) {
      gDisplay->setCursor(W - off + copy * period, y);
      gDisplay->print(_shown);
    }
  }
  gDisplay->setTextSize(1);
  gDisplay->fillRect(0, 13, 3, H - 13, cBlack);        // the big text ignores the clip: mask the edges
  gDisplay->fillRect(W - 3, 13, 3, H - 13, cBlack);

  // header: triangle, ALERT, triangle
  gDisplay->fillRect(0, 0, W, 13, cBlack);
  drawTriangle(2, 2, triA, markA);
  drawTriangle(W - 15, 2, triA, markA);
  gDisplay->setTextColor(phase ? cYel : cRed);
  gDisplay->setCursor((W - 5 * 6 + 1) / 2, 4);
  gDisplay->print(_preview ? "TEST!" : "ALERT");

  // flashing frame
  gDisplay->drawRect(0, 0, W, H, edge);

  gDisplay->flipDMABuffer();
}
