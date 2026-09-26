#include "LavaLamp.h"
#include <math.h>
#include <Fonts/TomThumb.h>
#include "../../core/Display.h"
#include "../../core/TimeService.h"
#include "../../core/WebUI.h"

static const unsigned long FRAME_MS = 40;

static String slider(const char *name, const char *label, uint8_t value, uint8_t lo, uint8_t hi, const char *suffix = "") {
  const String id = String(name) + "Value";
  return String("<label>") + label + ": <b id='" + id + "'>" + String(value) + suffix + "</b></label>"
         "<input type='range' id='" + name + "' name='" + name + "' min='" + lo + "' max='" + hi + "' value='" + value + "'>";
}

void LavaLampModule::onLoad() {
  _speed = store.getU8("spd", 100);
  if (_speed <= 2 || _speed < 25 || _speed > 200) _speed = 100;  // migrate the earlier three-step setting
  _blobCount = store.getU8("cnt", 8);
  if (_blobCount < 1 || _blobCount > BLOB_COUNT) _blobCount = BLOB_COUNT;
  _blobSize = store.getU8("size", 100);
  if (_blobSize < 50 || _blobSize > 150) _blobSize = 100;
  _trailFade = store.getU8("trail", 188);
  if (_trailFade < 120 || _trailFade > 235) _trailFade = 188;
  _cohesion = store.getU8("coh", 35);
  if (_cohesion > 100) _cohesion = 35;
  _convection = store.getU8("conv", 100);
  if (_convection > 200) _convection = 100;
  _sizeVariation = store.getU8("var", 35);
  if (_sizeVariation > 100) _sizeVariation = 35;
  _randomColors = store.getBool("rnd", false);
  _showClock = store.getBool("clk", false);
  _colA = store.getUInt("wax", 0xFF1400);
  _colB = store.getUInt("liq", 0xFF0037);
  _colC = store.getUInt("base", 0x6400FF);
  _glow = store.getUInt("glow", 0x100018);
}

String LavaLampModule::onSettingsHtml() {
  String h;
  h += uiSection("Motion");
  h += slider("spd", "Flow speed", _speed, 25, 200, "%");
  h += slider("cnt", "Blob count", _blobCount, 1, BLOB_COUNT);
  h += slider("size", "Blob size", _blobSize, 50, 150, "%");
  h += slider("trail", "Trail persistence", _trailFade, 120, 235);
  h += slider("coh", "Blob stickiness", _cohesion, 0, 100, "%");
  h += slider("conv", "Convection variation", _convection, 0, 200, "%");
  h += slider("var", "Blob size variation", _sizeVariation, 0, 100, "%");
  h += "<p class='m'>Higher convection adds turbulent rise, sink, and side-to-side movement. Higher size variation gives each blob a more distinct radius.</p>";
  h += uiCheckbox("clk", "Show digital time in the lower-left corner", _showClock);
  h += uiSection("Colors");
  h += uiCheckbox("rnd", "Use random colors for each blob", _randomColors);
  h += uiColor("wax", "Red-orange", _colA);
  h += uiColor("liq", "Hot pink", _colB);
  h += uiColor("base", "Purple", _colC);
  h += uiColor("glow", "Background glow", _glow);
  return h;
}

void LavaLampModule::onSave(WebServer &server) {
  _speed = (uint8_t)uiReadLong(server, "spd", _speed, 25, 200);
  _blobCount = (uint8_t)uiReadLong(server, "cnt", _blobCount, 1, BLOB_COUNT);
  _blobSize = (uint8_t)uiReadLong(server, "size", _blobSize, 50, 150);
  _trailFade = (uint8_t)uiReadLong(server, "trail", _trailFade, 120, 235);
  _cohesion = (uint8_t)uiReadLong(server, "coh", _cohesion, 0, 100);
  _convection = (uint8_t)uiReadLong(server, "conv", _convection, 0, 200);
  _sizeVariation = (uint8_t)uiReadLong(server, "var", _sizeVariation, 0, 100);
  _randomColors = server.hasArg("rnd");
  _showClock = server.hasArg("clk");
  uint32_t color;
  if (uiReadColor(server, "wax", color)) _colA = color;
  if (uiReadColor(server, "liq", color)) _colB = color;
  if (uiReadColor(server, "base", color)) _colC = color;
  if (uiReadColor(server, "glow", color)) _glow = color;
  store.putU8("spd", _speed);
  store.putU8("cnt", _blobCount);
  store.putU8("size", _blobSize);
  store.putU8("trail", _trailFade);
  store.putU8("coh", _cohesion);
  store.putU8("conv", _convection);
  store.putU8("var", _sizeVariation);
  store.putBool("rnd", _randomColors);
  store.putBool("clk", _showClock);
  store.putUInt("wax", _colA);
  store.putUInt("liq", _colB);
  store.putUInt("base", _colC);
  store.putUInt("glow", _glow);
  _frameW = _frameH = 0;               // restart with the new palette cleanly
}

String LavaLampModule::summary() {
  return String(_blobCount) + " soft " + (_randomColors ? "random-color" : "palette") +
         " blobs at " + String(_speed) + "% flow";
}

String LavaLampModule::actionsHtml() {
  return "<form method='POST' action='/lavalamp/colors-reset'><button class='sec' type='submit'>Reset colors to default</button></form>";
}

void LavaLampModule::registerRoutes(WebServer &server) {
  server.on("/lavalamp/colors-reset", HTTP_POST, [this]() {
    _colA = 0xFF1400;
    _colB = 0xFF0037;
    _colC = 0x6400FF;
    _glow = 0x100018;
    store.putUInt("wax", _colA);
    store.putUInt("liq", _colB);
    store.putUInt("base", _colC);
    store.putUInt("glow", _glow);
    _frameW = _frameH = 0;
    webRedirect("/lavalamp?saved=1");
  });
}

uint32_t LavaLampModule::nextRandom() {
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

uint32_t LavaLampModule::nextBlobColor(uint8_t paletteIndex) {
  static const uint32_t RANDOM_COLORS[] = {
    0xFF1744, 0xFF6D00, 0xFFD600, 0x63E61B, 0x00D5C8, 0x178BFF,
    0x7448FF, 0xCF38FF, 0xFF3DA7, 0xFFFFFF
  };
  if (_randomColors) return RANDOM_COLORS[nextRandom() % (sizeof(RANDOM_COLORS) / sizeof(RANDOM_COLORS[0]))];
  return paletteIndex % 5;
}

float LavaLampModule::nextRadius() {
  const float base = (3.5f + (nextRandom() % 7)) * _blobSize / 100.0f;
  const float spread = _sizeVariation / 100.0f;
  const float variation = ((int)(nextRandom() % 201) - 100) / 100.0f * spread;
  return max(1.5f, base * (1.0f + variation));
}

void LavaLampModule::resetBlobs(int width, int height, bool portrait) {
  (void)portrait;
  _frameW = width;
  _frameH = height;
  memset(_frame, 0, sizeof(_frame));
  const int along = height;             // blobs always fall down the physical screen
  const int cross = width;
  for (int i = 0; i < BLOB_COUNT; i++) {
    _blobs[i].radius = nextRadius();
    const bool rising = (i % 2) == 0;
    _blobs[i].along = rising ? along - 1 - _blobs[i].radius : _blobs[i].radius;
    _blobs[i].cross = 2.0f + (nextRandom() % (cross > 4 ? cross - 4 : 1));
    _blobs[i].temperature = rising ? 0.78f : 0.20f;
    _blobs[i].velAlong = rising ? -18.0f : 10.0f;
    _blobs[i].velCross = ((int)(nextRandom() % 9) - 4) * 0.35f;
    _blobs[i].color = nextBlobColor(i);
  }
}

static uint32_t scaleColor(uint32_t color, uint8_t amount) {
  return (((((color >> 16) & 255) * amount / 255) & 255) << 16) |
         (((((color >> 8) & 255) * amount / 255) & 255) << 8) |
         ((((color & 255) * amount / 255) & 255));
}

static uint32_t addColor(uint32_t base, uint32_t add) {
  const int r = min(255, (int)((base >> 16) & 255) + (int)((add >> 16) & 255));
  const int g = min(255, (int)((base >> 8) & 255) + (int)((add >> 8) & 255));
  const int b = min(255, (int)(base & 255) + (int)(add & 255));
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void LavaLampModule::render() {
  const int W = gDisplay->width(), H = gDisplay->height();
  const bool portrait = H > W;
  if (W != _frameW || H != _frameH) resetBlobs(W, H, portrait);

  const uint32_t palette[] = {
    _colA,
    0xFF4100,
    _colB,
    0xB40065,
    _colC
  };
  const float speed = _speed / 100.0f;

  // Gentle decay preserves trails without retaining bright, blocky frames.
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int i = y * W + x;
      _frame[i] = scaleColor(_frame[i], _trailFade);
      _frame[i] = addColor(_frame[i], scaleColor(_glow, 10));
    }
  }

  const int alongSize = H;
  const int crossSize = W;
  const float dt = (FRAME_MS / 1000.0f) * speed;
  const float cohesion = _cohesion / 100.0f;
  const float convection = _convection / 100.0f;
  for (int i = 0; i < _blobCount; i++) {
    for (int j = i + 1; j < _blobCount; j++) {
      Blob &a = _blobs[i];
      Blob &b = _blobs[j];
      const float da = b.along - a.along;
      const float dc = b.cross - a.cross;
      const float distance = sqrtf(da * da + dc * dc);
      const float reach = a.radius + b.radius + 5.0f;
      if (distance < 0.25f || distance >= reach) continue;
      const float pull = cohesion * (1.0f - distance / reach) * 30.0f * dt;
      a.velAlong += da / distance * pull;
      a.velCross += dc / distance * pull;
      b.velAlong -= da / distance * pull;
      b.velCross -= dc / distance * pull;
    }
  }
  for (int b = 0; b < _blobCount; b++) {
    Blob &blob = _blobs[b];
    // Heat at the lamp base makes the wax buoyant; cooling at the top makes it sink.
    const float bottomHeat = max(0.0f, (blob.along - alongSize * 0.65f) / max(1.0f, alongSize * 0.35f));
    const float topCooling = max(0.0f, (alongSize * 0.30f - blob.along) / max(1.0f, alongSize * 0.30f));
    const float turbulence = sinf(millis() * 0.0017f + b * 2.17f) * 0.10f * convection;
    blob.temperature += (bottomHeat * (2.80f + convection) - topCooling * (1.90f + convection * 0.6f)
                      - (blob.temperature - 0.48f) * 0.12f + turbulence) * dt;
    blob.temperature = max(0.0f, min(1.0f, blob.temperature));

    blob.velAlong += ((0.50f - blob.temperature) * 120.0f
                    + sinf(millis() * 0.0023f + b * 1.13f) * 18.0f * convection) * dt;
    blob.velAlong *= 0.97f;
    blob.velAlong = max(-20.0f, min(20.0f, blob.velAlong));
    const float circulation = sinf((blob.along / max(1, alongSize)) * 6.2831853f + millis() * 0.0008f + b * 1.7f);
    blob.velCross += (circulation * 18.0f + sinf(millis() * 0.0041f + b * 3.1f) * 15.0f * convection) * dt;
    blob.velCross *= 0.96f;
    blob.velCross = max(-9.0f, min(9.0f, blob.velCross));
    blob.along += blob.velAlong * dt;
    blob.cross += blob.velCross * dt;

    if (blob.along < blob.radius) {
      blob.along = blob.radius;
      if (blob.velAlong < 0) blob.velAlong *= -0.25f;
    } else if (blob.along > alongSize - 1 - blob.radius) {
      blob.along = alongSize - 1 - blob.radius;
      // Contact with the heated base gives a blob an immediate buoyant launch.
      blob.temperature = max(blob.temperature, 0.70f);
      if (blob.velAlong > 0) blob.velAlong = -max(18.0f, blob.velAlong * 0.25f);
    }
    if (blob.cross < blob.radius) {
      blob.cross = blob.radius;
      if (blob.velCross < 0) blob.velCross *= -0.35f;
    } else if (blob.cross > crossSize - 1 - blob.radius) {
      blob.cross = crossSize - 1 - blob.radius;
      if (blob.velCross > 0) blob.velCross *= -0.35f;
    }

    const int minAlong = max(0, (int)floorf(blob.along - blob.radius));
    const int maxAlong = min(alongSize - 1, (int)ceilf(blob.along + blob.radius));
    const int minCross = max(0, (int)floorf(blob.cross - blob.radius));
    const int maxCross = min(crossSize - 1, (int)ceilf(blob.cross + blob.radius));
    for (int along = minAlong; along <= maxAlong; along++) {
      for (int cross = minCross; cross <= maxCross; cross++) {
        const float da = along - blob.along, dc = cross - blob.cross;
        const float distance = sqrtf(da * da + dc * dc);
        if (distance >= blob.radius) continue;
        const float f = 1.0f - distance / blob.radius;
        const uint8_t intensity = (uint8_t)(f * f * 255.0f);
        const int x = cross;
        const int y = along;
        const int i = y * W + x;
        const uint32_t color = _randomColors ? blob.color : palette[blob.color % 5];
        _frame[i] = addColor(_frame[i], scaleColor(color, intensity));
      }
    }
  }

  gDisplay->fillScreen(0);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      gDisplay->drawPixel(x, y, rgb565(_frame[y * W + x]));
  if (_showClock) {
    struct tm now;
    if (timeNow(now)) {
      char time[6];
      snprintf(time, sizeof(time), "%02d:%02d", now.tm_hour, now.tm_min);
      gDisplay->setFont(&TomThumb);
      int16_t textX, textY;
      uint16_t textW, textH;
      gDisplay->getTextBounds(time, 0, 0, &textX, &textY, &textW, &textH);
      const int boxY = max(0, H - (int)textH - 4);
      gDisplay->fillRoundRect(0, boxY, min(W, (int)textW + 5), H - boxY, 2, 0);
      gDisplay->setTextColor(rgb565(0xFFF0D0));
      gDisplay->setCursor(2 - textX, H - 2 - textY - textH);
      gDisplay->print(time);
      gDisplay->setFont(nullptr);
    }
  }
  gDisplay->flipDMABuffer();
}

bool LavaLampModule::needsRedraw() {
  return millis() - _lastFrame >= FRAME_MS;
}

void LavaLampModule::drawPage(int sub) {
  (void)sub;
  _lastFrame = millis();
  render();
}
