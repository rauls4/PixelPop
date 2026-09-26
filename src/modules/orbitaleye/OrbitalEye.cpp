#include "OrbitalEye.h"
#include <math.h>
#include <Fonts/TomThumb.h>
#include "../../core/Display.h"
#include "../../core/WebUI.h"

static const char *const TERMINAL_DEFAULT_LINES[] = {
  "SYS>OK",
  "LINK OK",
  "OP>OPEN",
  "DENIED",
  "LOCKED",
  "ALL OK"
};
static const int TERMINAL_DEFAULT_LINE_COUNT = sizeof(TERMINAL_DEFAULT_LINES) / sizeof(TERMINAL_DEFAULT_LINES[0]);

static unsigned long terminalPixelMs(uint8_t pixelsPerSecond) {
  return 1000UL / pixelsPerSecond;
}

static uint16_t scaleColor(uint32_t color, float level) {
  if (level < 0) level = 0;
  if (level > 1) level = 1;
  const uint32_t r = (uint32_t)(((color >> 16) & 255) * level);
  const uint32_t g = (uint32_t)(((color >> 8) & 255) * level);
  const uint32_t b = (uint32_t)((color & 255) * level);
  return rgb565((r << 16) | (g << 8) | b);
}

static int terminalLineCount(const String &lines) {
  int count = 0;
  bool hasText = false;
  for (size_t i = 0; i <= lines.length(); i++) {
    if (i == lines.length() || lines[i] == '\n') {
      if (hasText) count++;
      hasText = false;
    } else if (lines[i] >= ' ') {
      hasText = true;
    }
  }
  return count;
}

static void terminalLineAt(const String &lines, int wanted, char *out, size_t cap) {
  int line = 0;
  size_t used = 0;
  bool hasText = false;
  for (size_t i = 0; i <= lines.length(); i++) {
    const char c = i == lines.length() ? '\n' : lines[i];
    if (c == '\n') {
      if (hasText && line++ == wanted) break;
      used = 0;
      hasText = false;
    } else if (c >= ' ') {
      hasText = true;
      if (line == wanted && used + 1 < cap) out[used++] = c;
    }
  }
  out[used] = 0;
}

void OrbitalEyeModule::onLoad() {
  _color = store.getUInt("col", 0xF02020);
  _speed = store.getU8("spd", 1);
  if (_speed > 2) _speed = 1;
  _terminalScrollSpeed = store.getU8("tspd", _speed == 0 ? 14 : (_speed == 2 ? 36 : 22));
  if (_terminalScrollSpeed < 8 || _terminalScrollSpeed > 36) _terminalScrollSpeed = 22;
  _terminalLines = store.getString("term", "");
  _portraitTerminal = store.getBool("pterm", false);
}

String OrbitalEyeModule::onSettingsHtml() {
  static const char *const speeds[] = {"Slow", "Normal", "Fast"};
  String h = uiSection("Lens");
  h += uiColor("col", "Glow color", _color);
  h += uiSelect("spd", "Pulse speed", speeds, 3, _speed);
  h += "<p class='m'>Portrait shows the breathing lens. Landscape keeps the lens on the left and adds an original green-on-black CRT terminal on the right.</p>";
  h += uiSection("Terminal");
  h += "<label>Preset terminal script</label><select onchange='terminalPreset(this.value)'>"
       "<option value=''>Choose a preset</option><option value='calm'>Original calm computer</option>"
       "<option value='diagnostic'>Built-in diagnostics</option></select>";
  h += "<label>Terminal lines (one per line)</label><textarea name='term' maxlength='240' rows='6' "
       "style='width:100%;box-sizing:border-box;padding:12px;font:16px -apple-system,Helvetica,Arial,sans-serif;"
       "border:1px solid #bbb;border-radius:8px;resize:vertical'>" + htmlEscape(_terminalLines) + "</textarea>"
       "<p class='m'>Landscape types each line in sequence. Choose a preset, then save to keep it. Leave this blank to use the built-in diagnostics.</p>"
       "<script>function terminalPreset(v){var t=document.getElementsByName('term')[0];"
       "if(v==='calm')t.value='GOOD EVENING, CREW.\\nALL SYSTEMS ARE STEADY.\\nNAVIGATION IS WITHIN TOLERANCE.\\nPLEASE REMAIN CALM.\\nAWAITING YOUR NEXT COMMAND.';"
       "else if(v==='diagnostic')t.value=''}</script>";
  h += "<label for='tspd'>Terminal scroll speed: <b id='tspdValue'>" + String(_terminalScrollSpeed) +
       "</b> pixels per second</label><input type='range' id='tspd' name='tspd' min='8' max='36' value='" +
       String(_terminalScrollSpeed) + "' oninput=\"document.getElementById('tspdValue').textContent=this.value\">";
  h += uiCheckbox("pterm", "Scroll terminal lines along the bottom in portrait", _portraitTerminal);
  return h;
}

void OrbitalEyeModule::onSave(WebServer &server) {
  uint32_t color;
  if (uiReadColor(server, "col", color)) _color = color;
  _speed = (uint8_t)uiReadLong(server, "spd", _speed, 0, 2);
  _terminalScrollSpeed = (uint8_t)uiReadLong(server, "tspd", _terminalScrollSpeed, 8, 36);
  _terminalLines = server.arg("term");
  _terminalLines.replace("\r", "");
  if (_terminalLines.length() > 240) _terminalLines.remove(240);
  _portraitTerminal = server.hasArg("pterm");
  store.putUInt("col", _color);
  store.putU8("spd", _speed);
  store.putU8("tspd", _terminalScrollSpeed);
  store.putString("term", _terminalLines);
  store.putBool("pterm", _portraitTerminal);
}

bool OrbitalEyeModule::needsRedraw() {
  return millis() - _lastFrame >= 45;
}

void OrbitalEyeModule::drawLandscapeTerminal(unsigned long now, int x, int width) {
  const int customLineCount = terminalLineCount(_terminalLines);
  const int lineCount = customLineCount > 0 ? customLineCount : TERMINAL_DEFAULT_LINE_COUNT;
  const uint16_t green = rgb565(0x62FF8A);
  const uint16_t dimGreen = rgb565(0x1D783A);
  const unsigned long pixelMs = terminalPixelMs(_terminalScrollSpeed);
  int longestLine = 0;
  char line[49];
  for (int i = 0; i < lineCount; i++) {
    if (customLineCount > 0) terminalLineAt(_terminalLines, i, line, sizeof(line));
    else snprintf(line, sizeof(line), "%s", TERMINAL_DEFAULT_LINES[i]);
    longestLine = max(longestLine, (int)strlen(line) * 4);
  }
  const unsigned long slotPixels = width + longestLine + 1;
  const unsigned long slot = (now / pixelMs) / slotPixels;
  const unsigned long position = (now / pixelMs) % slotPixels;
  const int active = (int)((slot * 1103515245UL + 12345UL) % (unsigned long)lineCount);
  if (customLineCount > 0) terminalLineAt(_terminalLines, active, line, sizeof(line));
  else snprintf(line, sizeof(line), "%s", TERMINAL_DEFAULT_LINES[active]);

  gDisplay->setFont(&TomThumb);
  gDisplay->setTextWrap(false);
  gDisplay->setTextColor(dimGreen);
  gDisplay->setCursor(x + 2, 6);
  gDisplay->print("TERM");
  gDisplay->drawFastHLine(x + 1, 7, width - 2, dimGreen);

  gDisplay->setTextColor(green);
  gDisplay->setClipX(x + 1, x + width - 1);
  gDisplay->setCursor(x + width - (int)position, 19);
  gDisplay->print(line);
  gDisplay->clearClip();
  gDisplay->setFont(nullptr);
}

void OrbitalEyeModule::drawPortraitTerminal(unsigned long now) {
  const int customLineCount = terminalLineCount(_terminalLines);
  const int lineCount = customLineCount > 0 ? customLineCount : TERMINAL_DEFAULT_LINE_COUNT;
  const int width = gDisplay->width();
  const unsigned long pixelMs = terminalPixelMs(_terminalScrollSpeed);
  int longestLine = 0;
  char line[49];
  for (int i = 0; i < lineCount; i++) {
    if (customLineCount > 0) terminalLineAt(_terminalLines, i, line, sizeof(line));
    else snprintf(line, sizeof(line), "%s", TERMINAL_DEFAULT_LINES[i]);
    longestLine = max(longestLine, (int)strlen(line) * 4);
  }
  const unsigned long slotPixels = width + longestLine + 1;
  const unsigned long slot = (now / pixelMs) / slotPixels;
  const unsigned long position = (now / pixelMs) % slotPixels;
  const int active = (int)((slot * 1103515245UL + 12345UL) % (unsigned long)lineCount);
  if (customLineCount > 0) terminalLineAt(_terminalLines, active, line, sizeof(line));
  else snprintf(line, sizeof(line), "%s", TERMINAL_DEFAULT_LINES[active]);

  const int height = gDisplay->height();
  gDisplay->fillRect(0, height - 7, width, 7, 0);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(rgb565(0x62FF8A));
  gDisplay->setClipX(0, width);
  gDisplay->setCursor(width - (int)position, height - 2);
  gDisplay->print(line);
  gDisplay->clearClip();
  gDisplay->setFont(nullptr);
}

void OrbitalEyeModule::drawPage(int sub) {
  (void)sub;
  const unsigned long now = millis();
  const int W = gDisplay->width(), H = gDisplay->height();
  const float speed = _speed == 0 ? 0.55f : (_speed == 2 ? 1.65f : 1.0f);
  const float phase = now * speed * 0.0024f;
  const float pulse = 0.5f + 0.5f * sinf(phase);

  const bool landscape = W >= H;
  gDisplay->fillScreen(0);
  const int leftW = landscape ? W * 17 / 32 : W;
  const int headerH = 7;
  const int blueW = landscape ? leftW / 2 - 1 : leftW * 7 / 16;
  gDisplay->fillRect(0, 0, leftW, headerH, 0);
  gDisplay->fillRect(0, 0, blueW, headerH, rgb565(0x124C8C));
  gDisplay->drawRect(0, 0, leftW, headerH, COL_WHITE);
  gDisplay->setFont(&TomThumb);
  gDisplay->setTextColor(COL_WHITE);
  const char *label = "HAL";
  gDisplay->setCursor(blueW - ((int)strlen(label) * 4 - 1) - 2, 6);
  gDisplay->print(label);
  gDisplay->setCursor(blueW + 2, 6);
  gDisplay->print("9000");

  gDisplay->setFont(nullptr);

  const int radius = landscape ? 10 : (W < H - headerH ? W : H - headerH) / 2 - 3;
  const int cx = leftW / 2;
  const int eyeY = headerH + (H - headerH) / 2;
  if (landscape) {
    const uint16_t lensBay = scaleColor(_color, 0.12f + 0.06f * pulse);
    gDisplay->fillRect(1, headerH + 1, leftW - 2, H - headerH - 2, lensBay);
    gDisplay->drawRect(1, headerH + 1, leftW - 2, H - headerH - 2, scaleColor(_color, 0.48f));
  }
  gDisplay->fillCircle(cx, eyeY, radius, scaleColor(_color, 0.42f + 0.18f * pulse));
  gDisplay->fillCircle(cx, eyeY, radius - 2, scaleColor(_color, 0.78f + 0.16f * pulse));
  gDisplay->fillCircle(cx, eyeY, radius - 5, scaleColor(_color, 0.96f));
  gDisplay->fillCircle(cx, eyeY, radius / 2, scaleColor(_color, 0.38f + 0.12f * pulse));
  gDisplay->fillCircle(cx, eyeY, radius / 2 - 2, 0);
  gDisplay->drawPixel(cx - (radius > 8 ? 2 : 1), eyeY - (radius > 8 ? 2 : 1), COL_WHITE);
  gDisplay->drawCircle(cx, eyeY, radius - 1, scaleColor(_color, 0.92f));
  if (landscape) drawLandscapeTerminal(now, leftW + 2, W - leftW - 3);
  else if (_portraitTerminal) drawPortraitTerminal(now);
  gDisplay->flipDMABuffer();
  _lastFrame = now;
}
