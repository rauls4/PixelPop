#include "Chime.h"
#include <math.h>
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"
#include "../../core/Speaker.h"
#include "../../core/Config.h"

// "12a", "1a" ... "12p", "1p" ...
static String hourLabel(int h) {
  h %= 24;
  int n = h % 12;
  if (n == 0) n = 12;
  return String(n) + (h < 12 ? "a" : "p");
}

static const char *const STYLE_NAMES[] = {
  "Westminster chimes (Big Ben)",
  "Cuckoo clock",
  "Grandfather clock (deep Westminster)"
};
static const char *const MODE_NAMES[] = {
  "Every minute",
  "Every 15 minutes (quarter hours)",
  "Hourly only"
};
static const char *const OUTPUT_NAMES[] = {
  "Onboard speaker (ES8311)",
  "External I2S amplifier (e.g. MAX98357A)"
};

// ---------- settings ----------
void ChimeModule::onLoad() {
  _style     = store.getU8("sty", 0);
  if (_style > 2) _style = 0;
  _mode      = store.getU8("mode", 0);
  if (_mode > 2) _mode = 0;
  _volume    = store.getU8("vol", 60);
  if (_volume < 1) _volume = 1;
  if (_volume > 100) _volume = 100;
  _hours     = store.getUInt("hrs", 0xFFFFFF) & 0xFFFFFF;
  if (_hours == 0xFFFFFF) {
    _startHour = 0;
    _endHour = 24;
  } else if (_hours != 0) {
    _startHour = 0;
    while (_startHour < 24 && !hourAllowed(_startHour)) _startHour++;
    _endHour = 24;
    while (_endHour > _startHour && !hourAllowed(_endHour - 1)) _endHour--;
  } else {
    _startHour = 8;
    _endHour = 20;
  }
  _output    = store.getU8("out", 0) ? 1 : 0;
  _pinBclk   = store.getU8("pbclk", 4);
  _pinWs     = store.getU8("pws", 5);
  _pinDout   = store.getU8("pdout", 6);
}

String ChimeModule::onSettingsHtml() {
  String h;
  h += uiSection("Sound");
  h += uiSelect("sty", "Chime style", STYLE_NAMES, 3, _style);
  h += "<p class='m'>Westminster: 1 phrase at :15, 2 at :30, 3 at :45, and the full chime with one bell "
       "strike per hour at :00. Cuckoo: one call at :15, :30 and :45, and one cuckoo per hour at :00. "
       "Grandfather clock uses slower, lower Westminster chimes and deep hour strikes.</p>";

  h += uiSection("When to chime");
  h += uiSelect("mode", "Chime", MODE_NAMES, 3, _mode);
  h += "<p class='m'>The traditional pattern is quarter hours. \"Every minute\" adds a short, quieter "
       "sound on all the other minutes.</p>";

  h += uiSection("Volume");
  h += uiNumber("vol", "Volume (1-100)", _volume, 1, 100);

  h += uiSection("Active hours");
  h += "<p class='m'>Drag either handle to choose the daily window when chimes can play.</p>"
       "<style>.hr{position:relative;height:42px;margin:12px 2px 4px}.hr .track,.hr .fill{position:absolute;top:18px;"
       "height:6px;border-radius:4px}.hr .track{left:0;right:0;background:#d1d1d6}.hr .fill{background:#0a66ff}"
       ".hr input{position:absolute;left:0;top:9px;width:100%;margin:0;padding:0;pointer-events:none;"
       "-webkit-appearance:none;appearance:none;background:transparent}.hr input::-webkit-slider-thumb{width:22px;height:22px;"
       "border-radius:50%;background:#0a66ff;border:2px solid #fff;box-shadow:0 1px 3px #0006;pointer-events:auto;"
       "-webkit-appearance:none}.hr input::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#0a66ff;"
       "border:2px solid #fff;box-shadow:0 1px 3px #0006;pointer-events:auto}.hr input::-webkit-slider-runnable-track{background:transparent}"
       ".hr input::-moz-range-track{background:transparent}.hrl{display:flex;justify-content:space-between;font-size:12px;color:#555}</style>";
  h += "<label>Chime window: <b id='hrv'></b></label><div class='hr'><div class='track'></div><div class='fill' id='hrf'></div>"
       "<input id='hs' name='hstart' type='range' min='0' max='23' value='" + String(_startHour) + "'>"
       "<input id='he' name='hend' type='range' min='1' max='24' value='" + String(_endHour) + "'></div>"
       "<div class='hrl'><span>12a</span><span>6a</span><span>12p</span><span>6p</span><span>12a</span></div>"
       "<p><button type='button' class='sec' style='width:auto;margin:10px 6px 0 0;padding:8px 12px;font-size:14px' onclick='hw(0,24)'>All day</button>"
       "<button type='button' class='sec' style='width:auto;margin:10px 0 0;padding:8px 12px;font-size:14px' onclick='hw(8,20)'>8am-8pm</button></p>"
       "<script>function hh(n){n=+n;var p=n>=12?'p':'a',h=n%12||12;return h+p}function hu(w){var s=document.getElementById('hs'),e=document.getElementById('he'),a=+s.value,b=+e.value;"
       "if(a>=b){if(w==='s')a=b-1;else b=a+1;s.value=a;e.value=b}document.getElementById('hrv').textContent=hh(a)+' to '+hh(b);"
       "var f=document.getElementById('hrf');f.style.left=(a/24*100)+'%';f.style.right=((24-b)/24*100)+'%'}function hw(a,b){document.getElementById('hs').value=a;document.getElementById('he').value=b;hu('e')}"
       "document.getElementById('hs').oninput=function(){hu('s')};document.getElementById('he').oninput=function(){hu('e')};hu('e')</script>";

  h += "<details style='margin-top:24px;padding-top:14px;border-top:1px solid #e3e3e8'>"
       "<summary style='cursor:pointer;font-size:17px;font-weight:600'>Advanced settings</summary>";
  h += uiSelect("out", "Audio output", OUTPUT_NAMES, 2, _output);
  h += "<p class='m'>Only change this if you wired your own I2S amplifier. Pins for it (GPIO numbers):</p>";
  h += uiNumber("pbclk", "BCLK pin", _pinBclk, 0, 48);
  h += uiNumber("pws", "LRCK / WS pin", _pinWs, 0, 48);
  h += uiNumber("pdout", "DATA pin", _pinDout, 0, 48);
  h += "</details>";
  return h;
}

void ChimeModule::onSave(WebServer &server) {
  _style     = (uint8_t)uiReadLong(server, "sty", _style, 0, 2);
  _mode      = (uint8_t)uiReadLong(server, "mode", _mode, 0, 2);
  _volume    = (uint8_t)uiReadLong(server, "vol", _volume, 1, 100);
  _startHour = (uint8_t)uiReadLong(server, "hstart", _startHour, 0, 23);
  _endHour = (uint8_t)uiReadLong(server, "hend", _endHour, 1, 24);
  if (_endHour <= _startHour) {
    if (_startHour < 23) _endHour = _startHour + 1;
    else { _startHour = 22; _endHour = 23; }
  }
  _hours = 0;
  for (int i = _startHour; i < _endHour; i++) {
    _hours |= (1UL << i);
  }
  _output    = (uint8_t)uiReadLong(server, "out", _output, 0, 1);
  _pinBclk   = (uint8_t)uiReadLong(server, "pbclk", _pinBclk, 0, 48);
  _pinWs     = (uint8_t)uiReadLong(server, "pws", _pinWs, 0, 48);
  _pinDout   = (uint8_t)uiReadLong(server, "pdout", _pinDout, 0, 48);

  store.putU8("sty", _style);
  store.putU8("mode", _mode);
  store.putU8("vol", _volume);
  store.putUInt("hrs", _hours);
  store.putU8("out", _output);
  store.putU8("pbclk", _pinBclk);
  store.putU8("pws", _pinWs);
  store.putU8("pdout", _pinDout);
}

String ChimeModule::summary() {
  String s = String(STYLE_NAMES[_style]) + " \xC2\xB7 " + MODE_NAMES[_mode];
  s += String(" \xC2\xB7 ") + hoursText();
  s += _busy ? " \xC2\xB7 playing now" : "";
  s += String(" \xC2\xB7 ") + _status;
  return s;
}

String ChimeModule::actionsHtml() {
  return "<h3 class='first'>Test the sound</h3>"
         "<p class='m'>Plays right away (through the speaker), so you can check the volume.</p>"
         "<form method='POST' action='/chime/test'>"
         "<button class='sec' type='submit' name='kind' value='0'>Minute chime</button>"
         "<button class='sec' type='submit' name='kind' value='1'>Quarter past</button>"
         "<button class='sec' type='submit' name='kind' value='2'>Half past</button>"
         "<button class='sec' type='submit' name='kind' value='3'>Quarter to</button>"
         "<button class='sec' type='submit' name='kind' value='4'>Full hour (current hour)</button>"
         "</form>"
         "<form method='POST' action='/chime/probe'>"
         "<button class='sec' type='submit'>Re-check the speaker</button></form>";
}

void ChimeModule::registerRoutes(WebServer &server) {
  server.on("/chime/test", HTTP_POST, [this, &server]() {
    if (!_busy) {
      int kind = (int)uiReadLong(server, "kind", 0, 0, 4);
      struct tm t;
      int h12 = 12;
      if (timeNow(t)) { h12 = t.tm_hour % 12; if (h12 == 0) h12 = 12; }
      startPlay((ChimeKind)kind, h12, kind == 0 ? 0.5f : 1.0f);
    }
    webRedirect("/chime");
  });
  server.on("/chime/probe", HTTP_POST, [this]() {
    if (!_busy) startProbe();
    webRedirect("/chime");
  });
}

// ---------- schedule ----------
// Short description of the selected hours, e.g. "8a-8p" or "6a-9a, 5p-9p"
String ChimeModule::hoursText() {
  if (_hours == 0) return "never (no hours ticked)";
  if (_hours == 0xFFFFFF) return "all day";
  String out;
  int i = 0;
  while (i < 24) {
    if (!hourAllowed(i)) { i++; continue; }
    int j = i;
    while (j + 1 < 24 && hourAllowed(j + 1)) j++;
    if (out.length()) out += ", ";
    out += hourLabel(i) + "-" + hourLabel(j + 1);
    i = j + 1;
  }
  return out;
}

// Decide what (if anything) to play at this minute.
bool ChimeModule::decide(const struct tm &t, ChimeKind &kind, float &level) {
  int m = t.tm_min;
  level = 1.0f;
  if (m == 0) { kind = CHIME_HOUR; return true; }
  if (_mode <= 1 && m % 15 == 0) {
    kind = (m == 15) ? CHIME_QUARTER1 : (m == 30) ? CHIME_HALF : CHIME_QUARTER3;
    return true;
  }
  if (_mode == 0) { kind = CHIME_TICK; level = 0.5f; return true; }
  return false;
}

void ChimeModule::begin() {
  _lastMinute = -1;
  _probeAt = millis() + 10000;  // keep I2C/audio initialization out of the boot burst
}

void ChimeModule::update() {
  if (_probeAt && millis() >= _probeAt) {
    _probeAt = 0;
    startProbe();               // find out whether the speaker answers
  }

  struct tm t;
  if (!timeNow(t)) return;

  // Chime on every new minute (not at boot: the first reading only sets the reference)
  if (t.tm_min == _lastMinute) return;
  bool first = (_lastMinute < 0);
  _lastMinute = t.tm_min;
  if (first || !enabled() || _busy || speakerUrgentIsOn()) return;
  if (!hourAllowed(t.tm_hour)) return;

  ChimeKind kind;
  float level;
  if (!decide(t, kind, level)) return;
  int h12 = t.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  startPlay(kind, h12, level);
}

// ---------- playback ----------
AudioConfig ChimeModule::makeConfig() {
  AudioConfig c;
  c.codec = (_output == 0);
  c.bclk = (int8_t)_pinBclk;
  c.ws   = (int8_t)_pinWs;
  c.dout = (int8_t)_pinDout;
  return c;
}

void ChimeModule::startPlay(ChimeKind kind, int hour12, float level) {
  if (_busy) return;
  _busy = true;
  _playStyle = (ChimeStyle)_style;
  _kind = kind;
  _hour12 = hour12;
  _levelMul = level;
  _selfTest = false;
  _cfg = makeConfig();
  speakerChimeRequest(true);                           // ask fireworks to step aside
  if (xTaskCreatePinnedToCore(taskEntry, "chime", 8192, this, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    speakerChimeRequest(false);
    _busy = false;
    snprintf(_status, sizeof(_status), "Could not start the playback task");
  }
}

void ChimeModule::startProbe() {
  if (_busy) return;
  _busy = true;
  _selfTest = true;
  _cfg = makeConfig();
  speakerChimeRequest(true);
  if (xTaskCreatePinnedToCore(taskEntry, "chimeprobe", 8192, this, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    speakerChimeRequest(false);
    _busy = false;
  }
}

void ChimeModule::taskEntry(void *arg) {
  ((ChimeModule *)arg)->playTask();
  vTaskDelete(nullptr);
}

void ChimeModule::playTask() {
  char msg[sizeof(_status)];

  // Fireworks (if playing) let go of the speaker before we start.
  if (!speakerWaitOthersReleased(6000)) {
    snprintf(_status, sizeof(_status), "Another sound did not release the speaker");
    speakerChimeRequest(false);
    _busy = false;
    return;
  }

  if (_selfTest) {
    // open and close without switching the amplifier on: no sound, just a status
    bool ok = _audio.open(_cfg, false, msg, sizeof(msg));
    if (ok) _audio.close();
    strncpy(_status, msg, sizeof(_status));
    _status[sizeof(_status) - 1] = '\0';
    Serial.print("Chime speaker: ");
    Serial.println(_status);
    speakerChimeRequest(false);
    _busy = false;
    return;
  }

  if (!_audio.open(_cfg, true, msg, sizeof(msg))) {
    strncpy(_status, msg, sizeof(_status));
    _status[sizeof(_status) - 1] = '\0';
    Serial.print("Chime speaker: ");
    Serial.println(_status);
    speakerChimeRequest(false);
    _busy = false;
    return;
  }
  strncpy(_status, msg, sizeof(_status));
  _status[sizeof(_status) - 1] = '\0';

  float v = _volume / 100.0f;
  float gain = v * v * _levelMul;                 // quadratic: feels more even to the ear
  _synth.start(_playStyle, _kind, _hour12, gain);

  static int16_t buf[256 * 2];
  while (!_synth.finished() && !speakerUrgentIsOn()) {       // an urgent notice cuts the chime short
    _synth.render(buf, 256);
    _audio.write(buf, 256);
  }
  _audio.close();
  speakerChimeRequest(false);
  _busy = false;
}
