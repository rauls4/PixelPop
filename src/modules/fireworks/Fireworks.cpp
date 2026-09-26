#include "Fireworks.h"
#include <math.h>
#include "../../core/Config.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"
#include "../../core/Speaker.h"
#include "../../core/AudioOut.h"

static const char *const PALETTE_NAMES[] = {
  "Multicolor", "Red, white and blue", "Gold and white", "Pick a color"
};
static const char *const DENSITY_NAMES[] = {"Slow and calm", "Medium", "Busy"};

static const float GRAVITY = 14.0f;         // pixels per second squared
static const unsigned long FRAME_MS = 40;   // 25 frames per second
static const unsigned long FINALE_MS = 2200;

// ---------- settings ----------
void FireworksModule::onLoad() {
  _palette   = store.getU8("pal", 0);
  if (_palette > 3) _palette = 0;
  _density   = store.getU8("den", 1);
  if (_density > 2) _density = 1;
  _finale    = store.getBool("fin", true);
  _skyline   = store.getBool("sky", true);
  _colCustom = store.getUInt("cCol", 0xFF3030);
  _sound     = store.getBool("snd", false);
  _soundVol  = store.getU8("svol", 50);
  if (_soundVol < 1) _soundVol = 1;
  if (_soundVol > 100) _soundVol = 100;
}

String FireworksModule::onSettingsHtml() {
  String h;
  h += uiSection("The show");
  h += uiSelect("pal", "Colors", PALETTE_NAMES, 4, _palette);
  h += uiColor("cCol", "Your color (used with \"Pick a color\")", _colCustom);
  h += uiSelect("den", "How busy", DENSITY_NAMES, 3, _density);
  h += uiCheckbox("fin", "Finale: a burst of rockets just before the page changes", _finale);
  h += uiCheckbox("sky", "Show a city skyline", _skyline);
  h += "<p class='m'>The show restarts each time this page comes around. "
       "\"Seconds on screen\" sets how long it runs.</p>";

  h += uiSection("Sound");
  h += uiCheckbox("snd", "Play sound with the show (whoosh, boom and crackle)", _sound);
  h += uiNumber("svol", "Sound volume (1-100)", _soundVol, 1, 100);
  h += "<p class='m'>Uses the onboard speaker. Chimes take over the speaker when they are due.</p>";
  return h;
}

void FireworksModule::onSave(WebServer &server) {
  _palette = (uint8_t)uiReadLong(server, "pal", _palette, 0, 3);
  _density = (uint8_t)uiReadLong(server, "den", _density, 0, 2);
  _finale  = server.hasArg("fin");
  _skyline = server.hasArg("sky");
  uint32_t c;
  if (uiReadColor(server, "cCol", c)) _colCustom = c;
  _sound    = server.hasArg("snd");
  _soundVol = (uint8_t)uiReadLong(server, "svol", _soundVol, 1, 100);
  store.putBool("snd", _sound);
  store.putU8("svol", _soundVol);
  store.putU8("pal", _palette);
  store.putU8("den", _density);
  store.putBool("fin", _finale);
  store.putBool("sky", _skyline);
  store.putUInt("cCol", _colCustom);
}

String FireworksModule::summary() {
  String s = String(PALETTE_NAMES[_palette]) + ", " + DENSITY_NAMES[_density];
  s += _sound ? ", with sound" : ", silent";
  return s;
}

String FireworksModule::actionsHtml() {
  String h = "<h3 class='first'>Test the sound</h3>"
             "<p class='m'>Plays a rocket, a boom and a crackle right away.</p>"
             "<form method='POST' action='/fireworks/test'><button class='sec' type='submit'>Play a test rocket</button></form>";
  if (_soundStatus[0]) h += String("<p class='m'>Speaker: ") + htmlEscape(_soundStatus) + "</p>";
  return h;
}

void FireworksModule::registerRoutes(WebServer &server) {
  server.on("/fireworks/test", HTTP_POST, [this]() {
    _testStartMs = millis() | 1;                // never 0
    if (!_soundRunning) startSound();
    webRedirect("/fireworks");
  });
}

// ---------- random helpers ----------
uint32_t FireworksModule::rnd() {          // xorshift32
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

float FireworksModule::frand(float a, float b) {
  return a + (b - a) * ((rnd() & 0xFFFF) / 65535.0f);
}

// hue 0..1 -> bright RGB
static void hueToRgb(float h, uint8_t &r, uint8_t &g, uint8_t &b) {
  float x = h * 6.0f;
  int i = (int)x;
  float f = x - i;
  float q = 1.0f - f;
  float rr, gg, bb;
  switch (i % 6) {
    case 0: rr = 1; gg = f; bb = 0; break;
    case 1: rr = q; gg = 1; bb = 0; break;
    case 2: rr = 0; gg = 1; bb = f; break;
    case 3: rr = 0; gg = q; bb = 1; break;
    case 4: rr = f; gg = 0; bb = 1; break;
    default: rr = 1; gg = 0; bb = q; break;
  }
  r = (uint8_t)(rr * 255); g = (uint8_t)(gg * 255); b = (uint8_t)(bb * 255);
}

void FireworksModule::pickColor(uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (_palette) {
    case 1: {                                   // red, white, blue
      int k = rnd() % 3;
      if (k == 0)      { r = 255; g = 30;  b = 30; }
      else if (k == 1) { r = 255; g = 255; b = 255; }
      else             { r = 50;  g = 90;  b = 255; }
      break;
    }
    case 2:                                     // gold and white
      if (rnd() % 3 == 0) { r = 255; g = 255; b = 230; }
      else                { r = 255; g = 170 + (rnd() % 50); b = 30; }
      break;
    case 3: {                                   // one color, slightly varied in brightness
      float k = frand(0.75f, 1.0f);
      r = (uint8_t)(((_colCustom >> 16) & 255) * k);
      g = (uint8_t)(((_colCustom >> 8) & 255) * k);
      b = (uint8_t)((_colCustom & 255) * k);
      break;
    }
    default:
      hueToRgb(frand(0.0f, 1.0f), r, g, b);
      break;
  }
}

// ---------- the show ----------
void FireworksModule::resetShow(unsigned long now) {
  for (int i = 0; i < MAX_ROCKETS; i++) _rk[i].on = false;
  for (int i = 0; i < MAX_SPARKS; i++) _sp[i].on = false;
  _rng ^= (uint32_t)now * 2654435761u;      // a different show each time
  if (_rng == 0) _rng = 2463534242u;
  _showStart = now;
  _nextLaunch = now + 200;
  _flash = 0;
}

void FireworksModule::launch() {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    if (_rk[i].on) continue;
    Rocket &r = _rk[i];
    r.on = true;
    r.x = frand(10, gDisplay->width() - 10);
    r.y = gDisplay->height() - 1;
    r.apexY = frand(5, 15);
    float rise = r.y - r.apexY;
    r.vy = -sqrtf(2.0f * GRAVITY * 0.6f * rise);        // rockets are lighter than sparks
    r.vx = frand(-3.0f, 3.0f);
    pickColor(r.r, r.g, r.b);
    pushSound(FW_LAUNCH, 0.8f, -r.vy / (GRAVITY * 0.6f));     // whoosh lasts as long as the climb
    return;
  }
}

void FireworksModule::addSpark(float x, float y, float ang, float speed, float life, float grav,
                               uint8_t r, uint8_t g, uint8_t b, bool glitter) {
  for (int i = 0; i < MAX_SPARKS; i++) {
    if (_sp[i].on) continue;
    Spark &s = _sp[i];
    s.on = true;
    s.x = x; s.y = y;
    s.vx = cosf(ang) * speed;
    s.vy = sinf(ang) * speed;
    s.life = s.maxLife = life;
    s.grav = grav;
    s.r = r; s.g = g; s.b = b;
    s.glitter = glitter;
    return;
  }
}

void FireworksModule::burst(const Rocket &rk) {
  const float TWO_PI_F = 6.2831853f;
  int type = rnd() % 5;                       // 0,1 sphere  2 ring  3 double ring  4 willow
  uint8_t r = rk.r, g = rk.g, b = rk.b;
  uint8_t r2, g2, b2;
  pickColor(r2, g2, b2);

  if (type <= 1) {                            // sphere
    int n = 46 + rnd() % 12;
    for (int i = 0; i < n; i++) {
      float sp = 27.0f * sqrtf(frand(0.08f, 1.0f));
      addSpark(rk.x, rk.y, frand(0, TWO_PI_F), sp, frand(1.1f, 1.8f), GRAVITY, r, g, b, false);
    }
  } else if (type == 2) {                     // ring
    int n = 30;
    float rot = frand(0, TWO_PI_F);
    for (int i = 0; i < n; i++) {
      addSpark(rk.x, rk.y, rot + i * TWO_PI_F / n, 22.0f, frand(1.3f, 1.6f), GRAVITY * 0.7f, r, g, b, false);
    }
  } else if (type == 3) {                     // double ring
    float rot = frand(0, TWO_PI_F);
    for (int i = 0; i < 26; i++)
      addSpark(rk.x, rk.y, rot + i * TWO_PI_F / 26, 25.0f, frand(1.3f, 1.6f), GRAVITY * 0.7f, r, g, b, false);
    for (int i = 0; i < 14; i++)
      addSpark(rk.x, rk.y, rot + i * TWO_PI_F / 14, 11.0f, frand(1.3f, 1.6f), GRAVITY * 0.7f, r2, g2, b2, false);
  } else {                                    // willow: slow golden sparks that droop
    int n = 42;
    for (int i = 0; i < n; i++) {
      addSpark(rk.x, rk.y, frand(0, TWO_PI_F), frand(6.0f, 18.0f), frand(2.0f, 3.0f), GRAVITY * 0.9f,
               255, 190, 60, true);
    }
  }
  _flashX = rk.x; _flashY = rk.y; _flash = 2;

  // sound: big bursts boom, rings are softer, willows crackle as they fall
  if (type <= 1)      { pushSound(FW_BOOM, 1.0f);      if (rnd() % 3 == 0) pushSound(FW_CRACKLE, 0.5f); }
  else if (type == 2) { pushSound(FW_BOOM_SOFT, 1.0f); }
  else if (type == 3) { pushSound(FW_BOOM, 0.9f); }
  else                { pushSound(FW_BOOM_SOFT, 0.9f); pushSound(FW_CRACKLE, 0.8f); }
}

void FireworksModule::step(float dt, unsigned long now) {
  // ---- launching ----
  unsigned long elapsed = now - _showStart;
  bool finale = _finale && elapsed + FINALE_MS >= (unsigned long)seconds() * 1000UL && elapsed > 3000;
  if (now >= _nextLaunch) {
    launch();
    if (finale) {
      launch();                                          // two at once
      _nextLaunch = now + 120 + rnd() % 180;
    } else {
      static const unsigned lo[3] = {1400, 800, 350};
      static const unsigned hi[3] = {2600, 1600, 900};
      _nextLaunch = now + lo[_density] + rnd() % (hi[_density] - lo[_density]);
    }
  }

  // ---- rockets ----
  for (int i = 0; i < MAX_ROCKETS; i++) {
    Rocket &r = _rk[i];
    if (!r.on) continue;
    r.vy += GRAVITY * 0.6f * dt;
    r.x += r.vx * dt;
    r.y += r.vy * dt;
    if (r.vy >= -2.0f || r.y <= r.apexY) {
      r.on = false;
      burst(r);
    }
  }

  // ---- sparks ----
  float drag = 1.0f - 1.8f * dt;
  if (drag < 0.5f) drag = 0.5f;
  for (int i = 0; i < MAX_SPARKS; i++) {
    Spark &s = _sp[i];
    if (!s.on) continue;
    s.vx *= drag;
    s.vy = s.vy * drag + s.grav * dt;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.life -= dt;
    if (s.life <= 0 || s.y >= gDisplay->height() || s.x < -2 || s.x > gDisplay->width() + 2) s.on = false;
  }
}

// ---------- drawing ----------
static inline uint16_t scaled(uint8_t r, uint8_t g, uint8_t b, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  return rgb565(((uint32_t)(r * k) << 16) | ((uint32_t)(g * k) << 8) | (uint32_t)(b * k));
}

static inline void px(int x, int y, uint16_t c) {
  gDisplay->drawPixel(x, y, c);
}

// A fixed little skyline: buildings of varying height with a few lit windows.
void FireworksModule::drawSkyline() {
  uint16_t wall = rgb565(0x0A0A1E);
  uint16_t win = rgb565(0x3A3010);
  int x = 0;
  uint32_t h = 12345;
  const int W = gDisplay->width(), H = gDisplay->height();
  while (x < W) {
    h = h * 1103515245u + 12345u;
    int w = 4 + (h >> 16) % 4;
    int ht = 3 + (h >> 20) % 4;
    for (int i = 0; i < w && x + i < W; i++) {
      gDisplay->drawFastVLine(x + i, H - ht, ht, wall);
    }
    if (ht >= 4 && w >= 5) px(x + 1 + (h >> 24) % (w - 2), H - ht + 1, win);
    x += w;
  }
}

void FireworksModule::render() {
  gDisplay->fillScreen(0);
  if (_skyline) drawSkyline();

  // rockets: a bright head with a short fading tail
  for (int i = 0; i < MAX_ROCKETS; i++) {
    const Rocket &r = _rk[i];
    if (!r.on) continue;
    px((int)r.x, (int)r.y, rgb565(0xFFFFFF));
    px((int)(r.x - r.vx * 0.05f), (int)(r.y - r.vy * 0.05f), scaled(255, 200, 120, 0.6f));
    px((int)(r.x - r.vx * 0.10f), (int)(r.y - r.vy * 0.10f), scaled(255, 150, 60, 0.3f));
  }

  // sparks: fade out as they age, with a dim trail
  for (int i = 0; i < MAX_SPARKS; i++) {
    const Spark &s = _sp[i];
    if (!s.on) continue;
    float k = powf(s.life / s.maxLife, 0.7f);      // gentle fade (LED panels look dim at low levels)
    if (s.glitter && (rnd() & 3) == 0) k *= 0.25f;       // twinkle
    px((int)s.x, (int)s.y, scaled(s.r, s.g, s.b, k));
    px((int)(s.x - s.vx * 0.04f), (int)(s.y - s.vy * 0.04f), scaled(s.r, s.g, s.b, k * 0.4f));
  }

  // brief white flash where a rocket burst
  if (_flash > 0) {
    uint16_t c = scaled(255, 255, 255, _flash == 2 ? 1.0f : 0.5f);
    px((int)_flashX, (int)_flashY, c);
    px((int)_flashX - 1, (int)_flashY, c);
    px((int)_flashX + 1, (int)_flashY, c);
    px((int)_flashX, (int)_flashY - 1, c);
    px((int)_flashX, (int)_flashY + 1, c);
    _flash--;
  }
  gDisplay->flipDMABuffer();
}

bool FireworksModule::needsRedraw() {
  return millis() - _lastFrame >= FRAME_MS;
}

void FireworksModule::drawPage(int sub) {
  (void)sub;
  unsigned long now = millis();
  // a long gap since the last frame means the page just came around again: new show
  bool newShow = (now - _lastFrame > 700);
  if (newShow) resetShow(now);
  if (_sound && !_soundRunning && now >= _soundRetryAt && !speakerChimeWaiting()) {
    _soundRetryAt = now + 2000;                // (also restarts after a chime interrupted it)
    startSound();
  }
  float dt = (now - _lastFrame) / 1000.0f;
  if (dt > 0.06f) dt = 0.06f;
  _lastFrame = now;
  step(dt, now);
  render();
}

// ---------- sound ----------
void FireworksModule::pushSound(FwSound s, float level, float seconds) {
  if (!_soundRunning) return;
  uint8_t next = (_qHead + 1) & 15;
  if (next == _qTail) return;                   // queue full: drop
  if (seconds < 0.4f) seconds = 0.4f;
  if (seconds > 6.0f) seconds = 6.0f;
  _q[_qHead] = {(uint8_t)s, (uint8_t)(level * 10.0f + 0.5f), (uint16_t)(seconds * 1000.0f)};
  _qHead = next;
}

void FireworksModule::startSound() {
  if (_soundRunning) return;
  _qHead = _qTail = 0;
  _soundRunning = true;
  if (xTaskCreatePinnedToCore(soundEntry, "fwsound", 8192, this, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    _soundRunning = false;
  }
}

void FireworksModule::soundEntry(void *arg) {
  ((FireworksModule *)arg)->soundTask();
  vTaskDelete(nullptr);
}

void FireworksModule::soundTask() {
  speakerFireworksActive(true);                 // claim the speaker, then check nobody else has it
  if (speakerChimeWaiting()) {
    speakerFireworksActive(false);
    snprintf(_soundStatus, sizeof(_soundStatus), "Silent: a chime has the speaker");
    _soundRunning = false;
    return;
  }

  AudioOut out;
  AudioConfig cfg;
  cfg.codec = true; cfg.bclk = cfg.ws = cfg.dout = -1;
  char msg[sizeof(_soundStatus)];
  if (!out.open(cfg, false, msg, sizeof(msg))) {
    snprintf(_soundStatus, sizeof(_soundStatus), "%s", msg);
    speakerFireworksActive(false);
    _soundRunning = false;
    return;
  }
  snprintf(_soundStatus, sizeof(_soundStatus), "%s", msg);

  FireworkSynth synth;
  synth.reset();
  int16_t buf[256 * 2];
  bool ampOn = false, testing = false;
  unsigned long lastSound = 0, testT0 = 0;
  int testStep = 0;

  for (;;) {
    unsigned long now = millis();
    if (_testStartMs) { testT0 = now; _testStartMs = 0; testing = true; testStep = 0; }
    bool pageActive = _sound && (now - _lastFrame) < 1500;
    if (!pageActive && !testing) break;
    if (speakerChimeWaiting()) break;   // the chime goes first

    // the Test button: a rocket, then its boom and crackle
    if (testing) {
      unsigned long dt = now - testT0;
      if (testStep == 0)                    { synth.trigger(FW_LAUNCH, 0.8f, 0.9f); testStep = 1; }
      if (testStep == 1 && dt >= 950)       { synth.trigger(FW_BOOM, 1.0f); synth.trigger(FW_CRACKLE, 0.7f); testStep = 2; }
      if (testStep == 2 && dt >= 3200)      testing = false;
    }

    // sounds queued by the show
    while (_qTail != _qHead) {
      SndEvent e = _q[_qTail];
      _qTail = (_qTail + 1) & 15;
      synth.trigger((FwSound)e.type, e.level10 / 10.0f, e.ms / 1000.0f);
    }

    // amplifier only on while something sounds (no hiss in between)
    if (!synth.idle()) {
      lastSound = now;
      if (!ampOn) { speakerAmp(true); ampOn = true; }
    } else if (ampOn && now - lastSound > 400) {
      speakerAmp(false); ampOn = false;
    }

    float v = _soundVol / 100.0f;
    synth.render(buf, 256, v * v);
    out.write(buf, 256);
  }

  out.close();
  speakerFireworksActive(false);
  _soundRunning = false;
}
