#include "Fire.h"
#include "../../core/Display.h"
#include "../../core/WebUI.h"

static const unsigned long FRAME_MS = 40;
static const char *const ROAST_NAMES[] = {"Off", "Random item", "Marshmallow", "Sausage"};

static String fireSlider(const char *name, const char *label, uint8_t value, uint8_t lo, uint8_t hi) {
  const String id = String(name) + "Value";
  return String("<label>") + label + ": <b id='" + id + "'>" + String(value) + "</b></label>"
         "<input type='range' id='" + name + "' name='" + name + "' min='" + lo + "' max='" + hi + "' value='" + value + "'>";
}

void FireModule::onLoad() {
  _cooling = store.getU8("cool", 65);
  if (_cooling < 20 || _cooling > 120) _cooling = 65;
  _sparking = store.getU8("spark", 125);
  if (_sparking < 10) _sparking = 125;
  _sparkHeight = store.getU8("height", 4);
  if (_sparkHeight < 1 || _sparkHeight > 8) _sparkHeight = 4;
  _roast = store.getU8("roast", 1);
  if (_roast > 3) _roast = 1;
}

String FireModule::onSettingsHtml() {
  String h;
  h += uiSection("Flame");
  h += fireSlider("cool", "Cooling", _cooling, 20, 120);
  h += fireSlider("spark", "Sparking", _sparking, 10, 220);
  h += fireSlider("height", "Ignition height", _sparkHeight, 1, 8);
  h += uiSection("Campfire roasting");
  h += uiSelect("roast", "Item on a stick", ROAST_NAMES, 4, _roast);
  h += "<p class='m'>A shaded marshmallow or sausage descends over the flames, browns until charred, then makes way for the next item.</p>";
  h += "<p class='m'>Higher cooling makes shorter flames. Higher sparking makes the base more active. "
       "The animation automatically follows landscape and portrait orientation.</p>";
  return h;
}

void FireModule::onSave(WebServer &server) {
  _cooling = (uint8_t)uiReadLong(server, "cool", _cooling, 20, 120);
  _sparking = (uint8_t)uiReadLong(server, "spark", _sparking, 10, 220);
  _sparkHeight = (uint8_t)uiReadLong(server, "height", _sparkHeight, 1, 8);
  _roast = (uint8_t)uiReadLong(server, "roast", _roast, 0, 3);
  store.putU8("cool", _cooling);
  store.putU8("spark", _sparking);
  store.putU8("height", _sparkHeight);
  store.putU8("roast", _roast);
  _simW = _simH = 0;
}

String FireModule::summary() {
  return String("Cooling ") + _cooling + ", sparking " + _sparking + ", " + ROAST_NAMES[_roast];
}

uint32_t FireModule::nextRandom() {
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

void FireModule::reset(int width, int height) {
  memset(_heat, 0, sizeof(_heat));
  _simW = width;
  _simH = height;
  _roastStarted = 0;
}

uint16_t FireModule::heatColor(uint8_t heat) const {
  const uint8_t ramp = (heat & 0x3F) << 2;
  uint8_t red = 0, green = 0, blue = 0;
  if (heat > 0xBF) {
    red = 255; green = 255; blue = ramp;
  } else if (heat > 0x7F) {
    red = 255; green = ramp; blue = 0;
  } else {
    red = ramp; green = 0; blue = 0;
  }
  return gDisplay->color565(red, green, blue);
}

bool FireModule::needsRedraw() {
  return millis() - _lastFrame >= FRAME_MS;
}

void FireModule::drawRoast(int width, int height) {
  if (_roast == 0) return;
  const unsigned long DESCEND_MS = 1600;
  const unsigned long ROAST_MS = 5200;
  const unsigned long now = millis();
  if (_roastStarted == 0 || now - _roastStarted >= DESCEND_MS + ROAST_MS) {
    _roastStarted = now;
    _roastKind = _roast == 1 ? (nextRandom() & 1 ? 2 : 3) : _roast;
    const int margin = 12;
    _roastX = margin + (int)(nextRandom() % max(1, width - margin * 2));
  }

  const unsigned long elapsed = now - _roastStarted;
  const int targetY = height - max(14, height / 4);
  const int foodY = elapsed < DESCEND_MS
    ? -10 + (int)((targetY + 10) * elapsed / DESCEND_MS)
    : targetY;
  const float roast = elapsed <= DESCEND_MS ? 0.0f
    : min(1.0f, (float)(elapsed - DESCEND_MS) / ROAST_MS);
  const uint16_t stick = rgb565(roast > 0.82f ? 0x3A1A08 : 0x9A5A20);
  const int handleX = width + 12;
  const int handleY = -12;
  const int stickX = _roastX + 3;
  gDisplay->drawLine(stickX, foodY, handleX, handleY, stick);
  gDisplay->drawLine(stickX + 1, foodY, handleX, handleY - 1, rgb565(0xD49A4A));

  const bool burnt = roast > 0.82f;
  if (_roastKind == 2) {
    const uint16_t outline = rgb565(burnt ? 0x1C120D : (roast > 0.45f ? 0x7A3712 : 0xCFA86A));
    const uint16_t body = rgb565(burnt ? 0x261814 : (roast > 0.45f ? 0xB85A1C : 0xFFF0C8));
    gDisplay->fillCircle(_roastX, foodY, 4, outline);
    gDisplay->fillCircle(_roastX, foodY, 3, body);
    if (!burnt) gDisplay->drawPixel(_roastX - 1, foodY - 1, rgb565(0xFFFFFF));
  } else {
    const uint16_t outline = rgb565(burnt ? 0x21100B : (roast > 0.45f ? 0x6B2110 : 0x8E3E22));
    const uint16_t body = rgb565(burnt ? 0x30150D : (roast > 0.45f ? 0xB9471B : 0xE86A38));
    gDisplay->fillRect(_roastX - 3, foodY - 3, 7, 7, outline);
    gDisplay->fillCircle(_roastX - 3, foodY, 3, outline);
    gDisplay->fillCircle(_roastX + 3, foodY, 3, outline);
    gDisplay->fillRect(_roastX - 3, foodY - 1, 7, 3, body);
    gDisplay->fillCircle(_roastX - 3, foodY, 1, body);
    gDisplay->fillCircle(_roastX + 3, foodY, 1, body);
    if (!burnt) gDisplay->drawLine(_roastX - 3, foodY - 1, _roastX + 3, foodY - 1, rgb565(0xFFB06A));
  }
}

void FireModule::drawPage(int sub) {
  (void)sub;
  const int width = gDisplay->width(), height = gDisplay->height();
  if (width != _simW || height != _simH) reset(width, height);

  // This is the supplied Fire2012-style algorithm. Simulation row zero is the
  // physical bottom regardless of the logical canvas orientation.
  const uint8_t coolRange = ((uint16_t)_cooling * 10 / max(1, height)) + 2;
  for (int x = 0; x < width; x++) {
    for (int y = 0; y < height; y++) {
      const uint8_t drop = nextRandom() % coolRange;
      _heat[y][x] = _heat[y][x] > drop ? _heat[y][x] - drop : 0;
    }
    for (int y = height - 1; y >= 2; y--) {
      _heat[y][x] = ((uint16_t)_heat[y - 1][x] + _heat[y - 2][x] + _heat[y - 2][x]) / 3;
    }
    if ((nextRandom() & 0xFF) < _sparking) {
      const int y = nextRandom() % min((int)_sparkHeight, height);
      const uint16_t boosted = _heat[y][x] + 160 + (nextRandom() & 95);
      _heat[y][x] = boosted > 255 ? 255 : boosted;
    }
  }

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      gDisplay->drawPixel(x, height - 1 - y, heatColor(_heat[y][x]));
    }
  }
  drawRoast(width, height);
  gDisplay->flipDMABuffer();
  _lastFrame = millis();
}
