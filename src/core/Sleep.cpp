#include "Sleep.h"
#include "SleepLogic.h"
#include "App.h"
#include "Display.h"
#include "Settings.h"
#include "TimeService.h"
#include "WebUI.h"

// ---------- settings ----------
enum { STYLE_OFF = 0, STYLE_CLOCK = 1 };

static SleepSched gS[2];
static uint8_t    gStyle = STYLE_OFF;
static uint8_t    gDim = 4;                 // brightness of the dim clock (the normal slider goes 5..100)
static bool       gUrgentWake = true;       // an urgent notice lights the panel up while it is showing
static uint32_t   gClockColor = 0xC02000;

// ---------- state ----------
static bool    gSched = false;              // what the schedule says right now
static uint8_t gOverride = 0;               // 0 follow the schedule, 1 sleep now, 2 wake now
static bool    gAsleep = false;
static int     gShownMin = -1;
static uint8_t gSavedOv = 0;                // what is stored in flash (it is only written when it changes)
static bool    gSavedSched = false;

static void saveState() {
  if (gOverride == gSavedOv && gSched == gSavedSched) return;
  Store g("");
  g.putU8("slOv", gOverride);
  g.putBool("slSc", gSched);
  gSavedOv = gOverride;
  gSavedSched = gSched;
}

static void load() {
  Store g("");
  static const char *const en[2] = {"sl1e", "sl2e"};
  static const char *const of[2] = {"sl1f", "sl2f"};
  static const char *const wk[2] = {"sl1n", "sl2n"};
  static const char *const dy[2] = {"sl1d", "sl2d"};
  for (int i = 0; i < 2; i++) {
    gS[i].enabled = g.getBool(en[i], false);
    gS[i].off = (uint16_t)(g.getUInt(of[i], i ? 13 * 60 : 22 * 60) % 1440);
    gS[i].wake = (uint16_t)(g.getUInt(wk[i], i ? 14 * 60 : 7 * 60) % 1440);
    gS[i].days = g.getU8(dy[i], 0x7F) & 0x7F;
  }
  gStyle = g.getU8("slSt", STYLE_OFF);
  if (gStyle > STYLE_CLOCK) gStyle = STYLE_OFF;
  gDim = g.getU8("slDim", 4);
  if (gDim < 1) gDim = 1;
  if (gDim > 30) gDim = 30;
  gUrgentWake = g.getBool("slUrg", true);
  gClockColor = g.getUInt("slCol", 0xC02000) & 0xFFFFFF;
  gOverride = g.getU8("slOv", 0);                      // the manual choice survives a restart
  if (gOverride > 2) gOverride = 0;
  gSched = g.getBool("slSc", false);                   // ...together with what the schedule said when it was made
  gSavedOv = gOverride;
  gSavedSched = gSched;
}

static void save() {
  Store g("");
  static const char *const en[2] = {"sl1e", "sl2e"};
  static const char *const of[2] = {"sl1f", "sl2f"};
  static const char *const wk[2] = {"sl1n", "sl2n"};
  static const char *const dy[2] = {"sl1d", "sl2d"};
  for (int i = 0; i < 2; i++) {
    g.putBool(en[i], gS[i].enabled);
    g.putUInt(of[i], gS[i].off);
    g.putUInt(wk[i], gS[i].wake);
    g.putU8(dy[i], gS[i].days);
  }
  g.putU8("slSt", gStyle);
  g.putU8("slDim", gDim);
  g.putBool("slUrg", gUrgentWake);
  g.putUInt("slCol", gClockColor);
}

void sleepBegin() { load(); }

// ---------- the dark / dim screen ----------
static void drawDimClock() {
  struct tm t;
  char buf[8];
  if (timeNow(t)) {
    int h = t.tm_hour;
    if (!app.use24Hour()) { h %= 12; if (h == 0) h = 12; }
    snprintf(buf, sizeof(buf), app.use24Hour() ? "%02d:%02d" : "%d:%02d", h, t.tm_min);
    gShownMin = t.tm_hour * 60 + t.tm_min;
  } else {
    snprintf(buf, sizeof(buf), "--:--");
    gShownMin = -2;
  }
  gDisplay->clearClip();
  gDisplay->fillScreen(0);
  gDisplay->setTextSize(2);
  gDisplay->setTextWrap(false);
  gDisplay->setTextColor(rgb565(gClockColor));
  const int w = (int)strlen(buf) * 12 - 2;
  int x = (gDisplay->width() - w) / 2;
  int y = (gDisplay->height() - 14) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  gDisplay->setCursor(x, y);
  gDisplay->print(buf);
  gDisplay->flipDMABuffer();
}

static void enterSleep() {
  if (gStyle == STYLE_CLOCK) {
    displaySetBrightness(gDim);
    drawDimClock();
  } else {
    gDisplay->clearClip();
    gDisplay->fillScreen(0);
    gDisplay->flipDMABuffer();
    displaySetBrightness(0);
    gShownMin = -1;
  }
}

bool sleepIsAsleep() { return gAsleep; }

void sleepWake() { gOverride = 2; saveState(); }

bool sleepTick(bool urgent, bool &woke) {
  woke = false;
  struct tm t;
  if (timeNow(t)) {
    const bool s = sleepScheduled(gS, 2, t.tm_wday, t.tm_hour * 60 + t.tm_min);
    if (s != gSched) { gSched = s; gOverride = 0; }          // the schedule changed its mind: it is in charge again
  }
  saveState();
  bool want = (gOverride == 1) ? true : (gOverride == 2) ? false : gSched;
  if (want && urgent && gUrgentWake) want = false;

  if (want && !gAsleep) {
    gAsleep = true;
    enterSleep();
  } else if (!want && gAsleep) {
    gAsleep = false;
    displaySetBrightness(app.brightness());
    woke = true;
  }

  if (gAsleep && gStyle == STYLE_CLOCK) {                    // keep the dim clock right
    int m = -2;
    if (timeNow(t)) m = t.tm_hour * 60 + t.tm_min;
    if (m != gShownMin) drawDimClock();
  }
  return gAsleep;
}

String sleepStatus() {
  if (gAsleep) {
    const bool manual = (gOverride == 1);
    return String(gStyle == STYLE_CLOCK ? "Display is dimmed to a clock" : "Display is off") +
           (manual ? " (turned off by hand)" : " (by the schedule)");
  }
  if (gOverride == 2 && gSched) return "Display is on (woken by hand; the schedule takes over again at its next change)";
  if (gS[0].enabled || gS[1].enabled) return "Display is on";
  return "Display is on (no schedule set)";
}

// ---------- web page ----------
static const char *const DAY_NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const uint8_t DAY_ORDER[7] = {1, 2, 3, 4, 5, 6, 0};     // Monday first

static String timeValue(uint16_t m) {
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", m / 60, m % 60);
  return String(b);
}

static bool readTime(const char *name, uint16_t &out) {
  if (!gServer.hasArg(name)) return false;
  String v = gServer.arg(name);
  int c = v.indexOf(':');
  if (c < 1) return false;
  int h = v.substring(0, c).toInt(), m = v.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  out = (uint16_t)(h * 60 + m);
  return true;
}

static void scheduleHtml(String &p, int i) {
  const String n = String(i + 1);
  p += String("<h3") + (i == 0 ? " class='first'" : "") + ">Schedule " + n + "</h3>";
  p += uiCheckbox((String("e") + n).c_str(), "Use this schedule", gS[i].enabled);
  p += String("<div class='tr'><div><label>Turn off at</label><input type='time' name='f") + n + "' value='" + timeValue(gS[i].off) + "'></div>"
       "<div><label>Turn on at</label><input type='time' name='n" + n + "' value='" + timeValue(gS[i].wake) + "'></div></div>";
  p += "<label>On these nights</label><div class='dy'>";
  for (int k = 0; k < 7; k++) {
    const int d = DAY_ORDER[k];
    p += String("<label><input type='checkbox' name='d") + n + "_" + d + "'" + (((gS[i].days >> d) & 1) ? " checked" : "") +
         ">" + DAY_NAMES[d] + "</label>";
  }
  p += "</div>";
}

static void handleSleepPage() {
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/html", "");
  String p = uiHead("Sleep");
  p += F("<style>.tr{display:flex;gap:12px}.tr>div{flex:1;min-width:0}"
         "input[type=time]{width:100%;box-sizing:border-box;padding:12px;font-size:16px;border:1px solid #bbb;border-radius:8px;background:#fff}"
         ".dy{display:flex;flex-wrap:wrap;gap:6px}.dy label{margin:0;font-weight:400;font-size:14px;padding:8px 10px;border:1px solid #bbb;border-radius:8px;background:#fff}"
         ".dy input{margin-right:5px}button.off{background:#3a3a3c}</style>");
  p += F("<nav><a href='/'>&#8249; Home</a></nav>");
  p += F("<div class='card'><h2>Sleep</h2>");
  if (gServer.hasArg("saved")) p += F("<div class='ok'>Saved.</div>");
  p += "<p class='m'>" + htmlEscape(sleepStatus()) + "</p>";
  p += F("<form method='POST' action='/sleep/now'>");
  if (gAsleep) p += F("<button name='state' value='on' type='submit'>Turn display on now</button>");
  else         p += F("<button class='off' name='state' value='off' type='submit'>Turn display off now</button>");
  p += F("<button class='sec' name='state' value='auto' type='submit'>Back to the schedule</button></form>"
         "<p class='m'>Tapping the BOOT button on the board also wakes a sleeping display.</p></div>");
  gServer.sendContent(p);
  p = "";

  p += F("<div class='card'><h2>Schedule</h2><form method='POST' action='/sleep'>"
         "<p class='m'>Pick the nights the display sleeps. A night is named after the day it starts: with Friday ticked and 22:00 to 07:00, "
         "it sleeps from Friday 22:00 until Saturday 07:00. The schedule starts working once the clock has been set from the internet.</p>");
  scheduleHtml(p, 0);
  gServer.sendContent(p);
  p = "";
  scheduleHtml(p, 1);
  static const char *const STYLES[2] = {"Screen completely off", "Very dim clock"};
  p += uiSection("While asleep");
  p += uiSelect("st", "Sleep screen", STYLES, 2, gStyle);
  p += uiNumber("dim", "Clock brightness (1 to 30)", gDim, 1, 30);
  p += uiColor("col", "Clock color", gClockColor);
  p += uiCheckbox("urg", "Urgent notices light the screen up", gUrgentWake);
  p += F("<button type='submit'>Save</button></form></div></body></html>");
  gServer.sendContent(p);
  gServer.sendContent("");
}

static void handleSleepSave() {
  for (int i = 0; i < 2; i++) {
    const String n = String(i + 1);
    gS[i].enabled = gServer.hasArg((String("e") + n).c_str());
    readTime((String("f") + n).c_str(), gS[i].off);
    readTime((String("n") + n).c_str(), gS[i].wake);
    uint8_t d = 0;
    for (int k = 0; k < 7; k++) if (gServer.hasArg((String("d") + n + "_" + k).c_str())) d |= (1 << k);
    gS[i].days = d;
  }
  gStyle = (uint8_t)uiReadLong(gServer, "st", gStyle, 0, 1);
  gDim = (uint8_t)uiReadLong(gServer, "dim", gDim, 1, 30);
  uint32_t c;
  if (uiReadColor(gServer, "col", c)) gClockColor = c;
  gUrgentWake = gServer.hasArg("urg");
  save();
  if (gAsleep) enterSleep();                                  // show the new look right away
  webRedirect("/sleep?saved=1");
}

static void handleSleepNow() {
  const String s = gServer.arg("state");
  if (s == "off") gOverride = 1;
  else if (s == "on") gOverride = 2;
  else gOverride = 0;
  saveState();
  app.requestRedraw();
  webRedirect(gServer.arg("back") == "/" ? "/" : "/sleep");             // only ever back to the home page or here
}

void sleepRegister(WebServer &server) {
  server.on("/sleep", HTTP_GET, handleSleepPage);
  server.on("/sleep", HTTP_POST, handleSleepSave);
  server.on("/sleep/now", HTTP_POST, handleSleepNow);
}
