#include "Advanced.h"
#include <WiFi.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define HAVE_HEAP_CAPS 1
#endif
#if __has_include(<nvs_flash.h>)
#include <nvs_flash.h>
#define HAVE_NVS_STATS 1
#endif
#include "WebUI.h"
#include "NetLock.h"
#include "Settings.h"
#include "App.h"
#include "Config.h"
#include "Net.h"
#include "CrashLog.h"
#include "Display.h"
#include "FwUpdate.h"

// ---------- update state ----------
static uint32_t      gCode = 0;              // the code on the panel (0 = none)
static unsigned long gCodeUntil = 0;
static uint8_t       gWrong = 0;             // wrong codes tried; too many cancels the code
static bool          gStarted = false;       // a file arrived
static bool          gActive = false;        // Update.begin worked and we are writing
static bool          gOk = false;
static char          gMsg[128] = "";
static size_t        gBytes = 0;
static size_t        gUploadTotal = 0;
static unsigned long gUploadProgressAt = 0;

static void fail(const char *m) { strncpy(gMsg, m, sizeof(gMsg) - 1); gMsg[sizeof(gMsg) - 1] = 0; }

// The 4-digit code shown on the panel guards every install. Returns nullptr if the code is right,
// otherwise a sentence saying why not. Five wrong tries cancel the code.
static const char *codeProblem(long code) {
  if (gCode == 0 || millis() > gCodeUntil) return "Press \"Show update code on the display\" first, then enter the code you see.";
  if ((uint32_t)code != gCode) {
    if (++gWrong >= 5) gCode = 0;
    return gCode ? "That is not the code on the display." : "Too many wrong codes. Show a new code on the display.";
  }
  return nullptr;
}

static void updateUpload() {
  HTTPUpload &u = gServer.upload();
  if (u.status == UPLOAD_FILE_START) {
    gStarted = true; gActive = false; gOk = false; gMsg[0] = 0; gBytes = 0;
    gUploadTotal = gServer.clientContentLength();
    gUploadProgressAt = 0;
    const char *why = codeProblem(gServer.arg("code").toInt());   // sent in the address, so it is known before the file
    if (why) {
      fail(why);
    } else if (fwStatus().state == FW_RUNNING) {
      fail("An update is already being downloaded. Wait for it to finish.");
    } else if (!esp_ota_get_next_update_partition(nullptr)) {
      fail("This display has no second firmware area, so it cannot be updated over Wi-Fi yet. See the notes above.");
    } else if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      fail(Update.errorString());
    } else {
      gActive = true;
      app.showFirmwareProgress(0);
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!gActive) return;
    if (gBytes == 0) {                                              // first block: is this a firmware for this chip?
      const char *bad = fwCheckHeader(u.buf, u.currentSize);
      if (bad) { fail(bad); Update.abort(); gActive = false; app.showMessage("Update", "cancelled", nullptr, 4000); return; }
    }
    if (Update.write(u.buf, u.currentSize) != u.currentSize) {
      fail(Update.errorString()); Update.abort(); gActive = false;
      app.showMessage("Update", "failed", nullptr, 6000);
      return;
    }
    const size_t before = gBytes;
    gBytes += u.currentSize;
    if (millis() - gUploadProgressAt >= 80 || gBytes / 65536 != before / 65536) {
      uint8_t pct = 0;
      if (gUploadTotal) {
        pct = (uint8_t)((uint64_t)gBytes * 100 / gUploadTotal);
        if (pct > 99) pct = 99;                                     // reserve 100% for Update.end()
      }
      app.showFirmwareProgress(pct);
      gUploadProgressAt = millis();
    }
    delay(1);                                                        // render progress and let Wi-Fi/idle tasks run
  } else if (u.status == UPLOAD_FILE_END) {
    if (!gActive) return;
    gActive = false;
    if (Update.end(true)) {
      gOk = true; gCode = 0;
      app.showFirmwareProgress(100);
    } else {
      fail(Update.errorString());
      app.showMessage("Update", "failed", nullptr, 6000);
    }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (gActive) Update.abort();
    gActive = false;
    app.showMessage("Update", "cancelled", nullptr, 4000);
  }
}

static void updateDone() {
  if (gOk) {
    gServer.send(200, "text/plain", "ok");
    delay(800);
    ESP.restart();
    return;
  }
  gServer.send(400, "text/plain", gStarted && gMsg[0] ? gMsg : "No file arrived. Choose the firmware file and try again.");
  gStarted = false;
}

// ---------- small helpers ----------
static String kv(const char *k, const String &v) {
  return String("<div class='kv'><span>") + k + "</span><b>" + v + "</b></div>";
}

static String upTime() {
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400UL, h = (s / 3600UL) % 24, m = (s / 60UL) % 60;
  String t;
  if (d) t += String((long)d) + " d ";
  if (d || h) t += String((long)h) + " h ";
  t += String((long)m) + " min";
  return t;
}

static String mb(uint32_t bytes) {
  return String((float)bytes / 1048576.0f, 1) + " MB";
}

static const char ADV_CSS[] =
  "<style>.kv{display:flex;justify-content:space-between;gap:14px;padding:9px 0;border-bottom:1px solid #eee}"
  ".kv:last-of-type{border-bottom:0}.kv span{color:#555}.kv b{font-weight:600;text-align:right;word-break:break-word}"
  "progress{width:100%;height:14px;margin-top:14px}"
  "input[type=file]{width:100%;box-sizing:border-box;padding:10px;font-size:15px;border:1px solid #bbb;border-radius:8px;background:#fff}"
  "ol.st{padding-left:20px;margin:6px 0 4px}ol.st li{margin:6px 0}"
  "details{margin-top:14px}summary{cursor:pointer;color:#0071e3;font-weight:600}h3{margin:22px 0 4px}</style>";

static const char ADV_JS[] =
  "<script>"
  "(function(){"
  "function $(i){return document.getElementById(i);}"
  "function say(i,t,bad){var e=$(i);if(e){e.textContent=t;e.className=bad?'err':'m';}}"
  "function post(u,d,cb,op){var x=new XMLHttpRequest();x.open('POST',u);if(op)x.upload.onprogress=op;"
  "x.onload=function(){cb(x.status,x.responseText);};x.onerror=function(){cb(0,'');};x.send(d||null);}"
  "function code(){var c=$('uc').value.trim();return /^[0-9]{4}$/.test(c)?c:'';}"
  "var CODEMSG='Press \"Show update code on the display\", then enter the 4-digit code you see there.';"
  // show the code on the display
  "var ab=$('ab');if(ab)ab.onclick=function(){post('/advanced/arm',null,function(s){"
  "say('as',s==200?'The code is on the display. It stays valid for five minutes.':'Could not show the code.',s!=200);});};"
  // look at the file
  "var cb=$('cb');if(cb)cb.onclick=function(){cb.disabled=true;say('cs','Looking at the file...');"
  "post('/advanced/check?u='+encodeURIComponent($('fu').value.trim()),null,function(s,t){"
  "cb.disabled=false;say('cs',t||'The display did not answer.',s!=200);});};"
  // download and install by the display itself
  "var fb=$('fb');if(fb)fb.onclick=function(){"
  "var c=code();if(!c){say('fs',CODEMSG,1);return;}"
  "var pr=$('fp');fb.disabled=true;pr.hidden=false;pr.value=0;say('fs','Starting...');"
  "post('/advanced/fetch?code='+c+'&u='+encodeURIComponent($('fu').value.trim())+'&md5='+encodeURIComponent($('fm').value.trim()),null,function(s,t){"
  "if(s!=200){fb.disabled=false;pr.hidden=true;say('fs',t||'Could not start.',1);return;}"
  "var lost=0;function reload(){location.href='/advanced';}"
  "function poll(){var x=new XMLHttpRequest();x.open('GET','/advanced/fstatus');"
  "x.onload=function(){lost=0;var r=x.responseText,i=r.indexOf('|'),j=r.indexOf('|',i+1),st=r.substring(0,i),n=+r.substring(i+1,j),m=r.substring(j+1);"
  "if(st=='run'){pr.value=n;say('fs',m);setTimeout(poll,1000);}"
  "else if(st=='done'){pr.value=100;say('fs',m+' This page reloads in a moment.');setTimeout(reload,25000);}"
  "else if(st=='fail'){fb.disabled=false;pr.hidden=true;say('fs',m,1);}"
  "else{fb.disabled=false;pr.hidden=true;say('fs','The display has no update running.',1);}};"
  "x.onerror=function(){if(++lost>=4){say('fs','The display stopped answering. If it restarted, the update is installed; this page reloads in a moment.');setTimeout(reload,20000);}else setTimeout(poll,1500);};"
  "x.send();}"
  "setTimeout(poll,1000);});};"
  // send a file from this computer
  "var b=$('ub');if(b){var pr2=$('up');b.onclick=function(){"
  "var c=code(),f=$('uf').files[0];"
  "if(!c){say('us',CODEMSG,1);return;}"
  "if(!f){say('us','Choose the firmware file (ending in .bin).',1);return;}"
  "b.disabled=true;pr2.hidden=false;pr2.value=0;say('us','Sending '+Math.round(f.size/1024)+' KB. Keep this page open.');"
  "var d=new FormData();d.append('firmware',f,f.name);"
  "post('/advanced/update?code='+c,d,function(s,t){"
  "if(s==200){pr2.value=100;say('us','Installed. The display is restarting; this page reloads in a moment.');setTimeout(function(){location.href='/';},25000);}"
  "else{b.disabled=false;pr2.hidden=true;say('us',t||'The update failed. Check that the display is still on, then try again.',1);}},"
  "function(e){if(e.lengthComputable)pr2.value=Math.round(e.loaded*100/e.total);});};}"
  "})();"
  "</script>";

static void handleAdvanced() {
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/html", "");
  String p = uiHead("Advanced");
  p += "<nav><a href='/'>&#8249; Home</a></nav>";
  p += FPSTR(ADV_CSS);
  p += "<div class='card'><h2>Advanced</h2>";
  if (gServer.hasArg("saved")) p += "<div class='ok'>Saved.</div>";
  p += "<p class='m'>Firmware and diagnostics. Most people never need this page.</p></div>";
  gServer.sendContent(p); p = "";

  // ---- firmware ----
  const esp_partition_t *run = esp_ota_get_running_partition();
  const esp_partition_t *nxt = esp_ota_get_next_update_partition(nullptr);
  String md5 = ESP.getSketchMD5();
  p += "<div class='card'><h2>Firmware</h2>";
  p += kv("Version", fwVersionText());
  p += kv("Build", md5.substring(0, 8));
  p += kv("Program size", mb(ESP.getSketchSize()));
  p += kv("Running from", run ? String(run->label) : String("?"));
  p += kv("Room for an update", nxt ? mb(nxt->size) : String("none"));
  gServer.sendContent(p); p = "";

  if (!nxt) {
    // Two program areas must both fit in the flash next to each other; say whether that is possible here.
    const uint32_t flash = ESP.getFlashChipSize();
    const uint32_t room = flash > 0x50000 ? flash - 0x50000 : 0;          // minus boot area and a little storage
    const uint32_t slot = (room / 2) & ~0xFFFFu;                            // each area is a multiple of 64 KB
    const uint32_t prog = ESP.getSketchSize();
    String m = "<div class='err' style='margin-top:14px'>This display has a single program area, so it cannot receive an update over Wi-Fi. "
               "Its flash memory is " + mb(flash) + ". Two program areas would each hold up to " + mb(slot) +
               ", and the program is " + mb(prog) + ", so ";
    m += (prog + 0x20000 <= slot) ? "two areas would fit. To turn updates on, put a <b>partitions.csv</b> that defines two program areas next to PixelPop.ino and upload once over USB (this erases stored photos)."
                                  : "two areas would <b>not</b> fit. Updates over Wi-Fi are not possible on this board with this program size; update over USB from the Arduino IDE.";
    m += "</div></div>";
    gServer.sendContent(m);
  } else {
    const bool autoEnabled = fwAutoEnabled();
    gServer.sendContent(
      "<h3>Official automatic updates</h3>"
      "<form method='POST' action='/advanced/autoupdate'>"
      "<label class='s'><input type='checkbox' name='auto'" + String(autoEnabled ? " checked" : "") +
      "> Automatically install new official PixelPop firmware</label>"
      "<p class='m'>When enabled, the display checks the official PixelPop firmware every 15 minutes and installs one newer published binary automatically. "
      "It turns itself off before installing, so a changed server response cannot cause an update/reboot loop. Enable it again for the next release. "
      "It only uses the official GitHub firmware address, never an address typed below.</p>"
      "<button class='sec' type='submit'>Save automatic update setting</button></form>"
      "<form method='POST' action='/advanced/autocheck'><button class='sec' type='submit'>Check official firmware now</button></form>"
      "<p class='m'>Status: " + htmlEscape(fwAutoStatus()) + "</p>");
    gServer.sendContent(
      "<h3>Update over the internet</h3>"
      "<p class='m'>The display downloads one firmware file from a web address and installs it by itself: no cable, no Arduino IDE. "
      "The file is the one ending in <b>.ino.bin</b> that <b>Sketch &rsaquo; Export Compiled Binary</b> makes. Put it anywhere that gives a direct "
      "https:// link to it, for example as a file attached to a GitHub release, and paste that link here.</p>"
      "<label for='fu'>Firmware address</label>");
    gServer.sendContent("<input type='url' id='fu' inputmode='url' maxlength='300' placeholder='https://.../PixelPop.ino.bin' value='" + htmlEscape(fwSavedUrl()) + "'>");
    gServer.sendContent(
      "<button class='sec' type='button' id='cb'>Check for update</button><p id='cs' class='m'></p>"
      "<details><summary>Checksum (optional)</summary>"
      "<label for='fm'>MD5 of the file</label><input type='text' id='fm' maxlength='32' placeholder='32 characters' autocomplete='off'>"
      "<p class='m'>If you enter the file's MD5 checksum, the display refuses to install a file that does not match it.</p></details>"
      "<h3>Confirm on the display</h3>"
      "<p class='m'>To stop anyone else on your Wi-Fi from installing firmware, the display shows a 4-digit code that you type here.</p>"
      "<button class='sec' type='button' id='ab'>Show update code on the display</button><p id='as' class='m'></p>"
      "<label for='uc'>Code from the display</label><input type='number' id='uc' inputmode='numeric' min='1000' max='9999' placeholder='4 digits'>"
      "<button type='button' id='fb'>Download and install</button>"
      "<progress id='fp' max='100' value='0' hidden></progress><p id='fs' class='m'></p>"
      "<details><summary>Or install a file from this computer</summary>"
      "<label for='uf'>Firmware file (.ino.bin)</label><input type='file' id='uf' accept='.bin,application/octet-stream'>"
      "<button type='button' id='ub'>Install this file</button>"
      "<progress id='up' max='100' value='0' hidden></progress><p id='us' class='m'></p></details>"
      "<p class='m'>The download takes about a minute and the display restarts by itself. "
      "If a new firmware ever fails to start, install a working one again over USB from the Arduino IDE.</p></div>");
  }

  // ---- versions of every function ----
  p += "<div class='card'><h2>Versions</h2>";
  p += kv("Firmware", FW_VERSION);
  for (int i = 0; i < app.count(); i++) {
    Module *m = app.module(i);
    p += kv(m->title(), m->version());
    if (p.length() > 1200) { gServer.sendContent(p); p = ""; }
  }
  p += "</div>";
  gServer.sendContent(p); p = "";

  // ---- panel color ----
  p += "<div class='card'><h2>Panel color</h2>"
       "<p class='m'>Currently: " + String(colorOrderName(app.colorOrder())) +
       ", red " + String(app.gainR()) + "/255, green " + String(app.gainG()) + "/255, blue " + String(app.gainB()) + "/255."
       "</p><p class='m'>If colors still look wrong after trying the wiring options, Color Lab adds live, on-panel intensity "
       "sliders for each channel &mdash; use it if a plain wiring swap is not enough (for example, colors are wrong but not "
       "simply swapped).</p>"
       "<form method='GET' action='/advanced/colorlab'><button class='sec' type='submit'>Open Color Lab</button></form></div>";
  gServer.sendContent(p); p = "";

  // ---- diagnostics ----
  p += "<div class='card'><h2>Diagnostics</h2>";
  p += kv("Uptime", upTime());
  p += kv("Address", WiFi.localIP().toString() + " (" + netHostname() + ".local)");
  p += kv("Wi-Fi signal", String(WiFi.RSSI()) + " dBm");
  p += kv("MAC address", WiFi.macAddress());
  p += kv("Display rotation", String((unsigned)app.rotation() * 90) + " degrees");
  p += kv("Orientation sensor", app.imuAvailable() ? String(app.autoOrientation() ? "QMI8658 active at 0x" : "QMI8658 detected; manual mode at 0x") + String(app.imuAddress(), HEX) : String("QMI8658 unavailable"));
  {
    const OrientationSample imu = app.imuSample();
    if (imu.valid) {
      p += kv("IMU acceleration",
              "X " + String((float)imu.x / 16384.0f, 2) + " g, Y " +
              String((float)imu.y / 16384.0f, 2) + " g, Z " +
              String((float)imu.z / 16384.0f, 2) + " g");
    } else if (app.imuAvailable()) {
      p += kv("IMU acceleration", "Waiting for first sample");
    } else {
      p += kv("IMU acceleration", "Unavailable; check the serial log after startup");
    }
  }
#ifdef HAVE_HEAP_CAPS
  p += kv("Lowest free memory", String((unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024)) + " KB since start");
#endif
  for (int i = 0; i < memMarkCount(); i++) {
    const MemMark &m = memMarkAt(i);
    p += kv((String("At: ") + m.label).c_str(), String(m.freeK) + " KB free, block " + String(m.blockK) + " KB, extra " + String(m.extraK) + " KB");
  }
#ifdef HAVE_HEAP_CAPS
  p += kv("Memory free", String((unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024)) + " KB");
  p += kv("Largest free block", String((unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024)) + " KB");
#else
  p += kv("Memory free", String((unsigned)(ESP.getFreeHeap() / 1024)) + " KB");
#endif
  p += kv("Extra memory free", mb(ESP.getFreePsram()));
  p += kv("Flash size", mb(ESP.getFlashChipSize()));
#ifdef HAVE_NVS_STATS
  {
    nvs_stats_t st;
    if (nvs_get_stats(nullptr, &st) == ESP_OK)
      p += kv("Saved settings space", String((unsigned)st.used_entries) + " of " + String((unsigned)st.total_entries) + " slots used");
  }
#endif
  {
    const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    p += kv("Settings storage", nvs ? String((unsigned)(nvs->size / 1024)) + " KB" : String("not found"));
    if (nvs && nvs->size < 0x10000)
      p += "<div class='err'>This board has an older, smaller settings partition. Upload the current firmware once over USB to install the required 64 KB layout; an over-the-air update cannot change it.</div>";
  }
  p += kv("Settings not saved", storeFailures() == 0 ? String("none") : String((unsigned)storeFailures()) + " (last: " + storeLastFailure() + ", " + storeLastReason() + ")");
  p += "<p class='m' style='margin-top:14px'>Secure connections (weather, news, flights) need a large block of free memory. "
       "If \"Largest free block\" is under about 40 KB, downloads can fail.</p>";
  p += "<h3>Recent resets</h3>";
  const uint8_t crashCount = crashLogCount();
  if (crashCount == 0) {
    p += "<p class='m'>No resets have been recorded yet.</p>";
  } else {
    p += "<p class='m'>Newest first; keeps the last " + String(CRASH_LOG_SIZE) + " resets, including intentional ones.</p><ol class='st'>";
    for (uint8_t i = 0; i < crashCount; i++) {
      const esp_reset_reason_t reason = (esp_reset_reason_t)crashLogReasonAt(i);
      p += "<li>" + String(crashLogReasonName(reason)) + " (" + String((int)reason) + ")</li>";
    }
    p += "</ol><form method='POST' action='/advanced/crashes/clear' onsubmit=\"return confirm('Clear the recorded crash history?')\">"
         "<button class='sec' type='submit'>Clear crash history</button></form>";
  }
  p += "<form method='POST' action='/advanced/restart' onsubmit=\"return confirm('Restart the display now?')\">"
       "<button class='sec' type='submit'>Restart the display</button></form></div>";
  p += FPSTR(ADV_JS);
  p += "</body></html>";
  gServer.sendContent(p);
  gServer.sendContent("");
}

// ---------- Color Lab: live, on-panel color calibration ----------
// Opening this page puts the panel into calibration mode (see App::calStart): the normal page
// rotation pauses and the panel shows whatever swatch/order/gain this page last asked for. Every
// slider or button here calls /advanced/colorlab/live right away, so the panel updates within
// about a tenth of a second; nothing is kept after a restart until Save is pressed. Leaving the
// page without pressing Done is fine too: App::loop() leaves calibration mode by itself after ten
// idle minutes.
static void handleColorLab() {
  app.calStart();
  // This GET has a real side effect (calStart pauses rotation), so a cached/back-forward
  // load must never be served without re-running it.
  gServer.sendHeader("Cache-Control", "no-store");
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/html", "");
  String p = uiHead("Color Lab");
  p += "<nav><a href='/advanced'>&#8249; Advanced</a></nav>";
  p += "<div class='card'><h2>Color Lab</h2>"
       "<p class='m'>Live on the panel as you adjust &mdash; nothing is saved until you tap Save. "
       "Leaves this mode by itself after ten minutes idle, or tap Done when you are finished.</p>"
       "<div id='clmsg' class='ok' style='display:none'></div>";
  p += "<label for='cl_order'>Wiring of the red, green and blue channels</label><select id='cl_order'>";
  for (int i = 0; i < COLOR_ORDERS; i++) {
    p += "<option value='" + String(i) + "'" + String(i == app.colorOrder() ? " selected" : "") + ">" + colorOrderName(i) + "</option>";
  }
  p += "</select>";
  p += "<label>Red intensity: <b id='cl_rv'>" + String(app.gainR()) + "</b></label>"
       "<input type='range' id='cl_r' min='0' max='255' value='" + String(app.gainR()) + "'>";
  p += "<label>Green intensity: <b id='cl_gv'>" + String(app.gainG()) + "</b></label>"
       "<input type='range' id='cl_g' min='0' max='255' value='" + String(app.gainG()) + "'>";
  p += "<label>Blue intensity: <b id='cl_bv'>" + String(app.gainB()) + "</b></label>"
       "<input type='range' id='cl_b' min='0' max='255' value='" + String(app.gainB()) + "'>";
  gServer.sendContent(p); p = "";

  p += "<label>Test color shown on the panel</label>"
       "<div style='display:flex;flex-wrap:wrap;gap:6px;margin-top:6px'>";
  const CalSwatch *sw = calSwatches();
  int defaultSwatch = 0;
  for (int i = 0; i < CAL_SWATCH_COUNT; i++) {
    char hex[8]; snprintf(hex, sizeof(hex), "#%02x%02x%02x", sw[i].r, sw[i].g, sw[i].b);
    const long lum = 299L * sw[i].r + 587L * sw[i].g + 114L * sw[i].b;
    const char *fg = lum > 150000L ? "#000" : "#fff";
    if (strcmp(sw[i].name, "White") == 0) defaultSwatch = i;
    p += "<button type='button' class='clsw' data-i='" + String(i) +
         "' style='flex:1 1 30%;min-width:80px;margin:0;padding:10px 4px;border:0;border-radius:8px;background:" +
         hex + ";color:" + fg + ";font-size:14px'>" + sw[i].name + "</button>";
  }
  p += "</div>";
  p += "<button type='button' id='cl_save'>Save</button>"
       "<button type='button' class='sec' id='cl_done'>Done (leave Color Lab)</button>"
       "</div>";
  gServer.sendContent(p); p = "";

  String js = "<script>(function(){";
  js += "var curSwatch=" + String(defaultSwatch) + ",t=null;";
  js += F(
    "function $(i){return document.getElementById(i);}"
    "function say(m,bad){var e=$('clmsg');e.style.display='block';e.textContent=m;e.className=bad?'err':'ok';}"
    "function post(u,cb){var x=new XMLHttpRequest();x.open('POST',u);x.onload=function(){cb&&cb(x.status);};x.send();}"
    "function state(){return 'order='+$('cl_order').value+'&r='+$('cl_r').value+'&g='+$('cl_g').value+'&b='+$('cl_b').value+'&swatch='+curSwatch;}"
    "function apply(){clearTimeout(t);t=setTimeout(function(){post('/advanced/colorlab/live?'+state());},60);}"
    "$('cl_r').addEventListener('input',function(){$('cl_rv').textContent=this.value;apply();});"
    "$('cl_g').addEventListener('input',function(){$('cl_gv').textContent=this.value;apply();});"
    "$('cl_b').addEventListener('input',function(){$('cl_bv').textContent=this.value;apply();});"
    "$('cl_order').addEventListener('change',apply);"
    "Array.prototype.forEach.call(document.querySelectorAll('.clsw'),function(b){b.addEventListener('click',function(){curSwatch=this.dataset.i;apply();});});"
    "$('cl_save').addEventListener('click',function(){post('/advanced/colorlab/save?'+state(),function(s){say(s==200?'Saved.':'Could not save.',s!=200);});});"
    "$('cl_done').addEventListener('click',function(){post('/advanced/colorlab/exit',function(){location.href='/advanced';});});"
    "apply();"
  );
  js += "})();</script>";
  gServer.sendContent(js);
  gServer.sendContent("</body></html>");
  gServer.sendContent("");
}

void advancedRegister(WebServer &server) {
  server.on("/advanced", HTTP_GET, handleAdvanced);
  server.on("/advanced/arm", HTTP_POST, []() {
    gCode = 1000 + (esp_random() % 9000);
    gCodeUntil = millis() + 5UL * 60UL * 1000UL;
    gWrong = 0;
    char c[8]; snprintf(c, sizeof(c), "%u", (unsigned)gCode);
    app.showAccessCode(gCode, 30000);
    gServer.send(200, "text/plain", "ok");
  });
  server.on("/advanced/autoupdate", HTTP_POST, []() {
    fwAutoSetEnabled(gServer.hasArg("auto"));
    webRedirect("/advanced?saved=1");
  });
  server.on("/advanced/autocheck", HTTP_POST, []() {
    if (!fwAutoEnabled()) {
      gServer.send(400, "text/plain", "Enable official automatic updates first.");
      return;
    }
    if (!fwAutoCheckNow()) {
      gServer.send(409, "text/plain", "An update check or install is already running.");
      return;
    }
    webRedirect("/advanced");
  });
  server.on("/advanced/check", HTTP_POST, []() {
    String u = gServer.arg("u");
    const char *bad = fwCleanUrl(u);
    if (bad) { gServer.send(400, "text/plain", bad); return; }
    fwSaveUrl(u);
    bool ok = false;
    const String r = fwCheck(u, ok);
    gServer.send(ok ? 200 : 400, "text/plain", r);
  });
  server.on("/advanced/fetch", HTTP_POST, []() {
    const char *why = codeProblem(gServer.arg("code").toInt());
    if (why) { gServer.send(400, "text/plain", why); return; }
    if (!esp_ota_get_next_update_partition(nullptr)) { gServer.send(400, "text/plain", "This display has no second firmware area, so it cannot be updated over Wi-Fi yet."); return; }
    String u = gServer.arg("u");
    const char *bad = fwCleanUrl(u);
    if (bad) { gServer.send(400, "text/plain", bad); return; }
    fwSaveUrl(u);
    String r;
    if (!fwStart(u, gServer.arg("md5"), r)) { gServer.send(400, "text/plain", r); return; }
    gServer.send(200, "text/plain", "started");
  });
  server.on("/advanced/fstatus", HTTP_GET, []() {
    const FwStatus st = fwStatus();
    String o;
    if (st.state == FW_RUNNING) {
      o = "run|" + String((int)st.pct) + "|";
      if (st.total) o += "Downloading " + String((unsigned)(st.bytes / 1024)) + " of " + String((unsigned)(st.total / 1024)) + " KB"; else o += st.msg;
    } else if (st.state == FW_DONE) o = String("done|100|") + st.msg;
    else if (st.state == FW_FAILED) o = String("fail|0|") + st.msg;
    else o = "idle|0|";
    gServer.send(200, "text/plain", o);
  });
  server.on("/advanced/colorlab", HTTP_GET, handleColorLab);
  server.on("/advanced/colorlab/live", HTTP_POST, []() {
    const uint8_t order = (uint8_t)uiReadLong(gServer, "order", app.colorOrder(), 0, COLOR_ORDERS - 1);
    const uint8_t r = (uint8_t)uiReadLong(gServer, "r", app.gainR(), 0, 255);
    const uint8_t g = (uint8_t)uiReadLong(gServer, "g", app.gainG(), 0, 255);
    const uint8_t b = (uint8_t)uiReadLong(gServer, "b", app.gainB(), 0, 255);
    const int swatch = (int)uiReadLong(gServer, "swatch", -1, -1, CAL_SWATCH_COUNT - 1);
    app.calPreview(order, r, g, b, swatch);
    gServer.send(200, "text/plain", "ok");
  });
  server.on("/advanced/colorlab/save", HTTP_POST, []() {
    const uint8_t order = (uint8_t)uiReadLong(gServer, "order", app.colorOrder(), 0, COLOR_ORDERS - 1);
    const uint8_t r = (uint8_t)uiReadLong(gServer, "r", app.gainR(), 0, 255);
    const uint8_t g = (uint8_t)uiReadLong(gServer, "g", app.gainG(), 0, 255);
    const uint8_t b = (uint8_t)uiReadLong(gServer, "b", app.gainB(), 0, 255);
    app.calSave(order, r, g, b);
    gServer.send(200, "text/plain", "ok");
  });
  server.on("/advanced/colorlab/exit", HTTP_POST, []() {
    app.calExit();
    gServer.send(200, "text/plain", "ok");
  });
  server.on("/advanced/crashes/clear", HTTP_POST, []() {
    crashLogClear();
    webRedirect("/advanced");
  });
  server.on("/advanced/update", HTTP_POST, updateDone, updateUpload);
  server.on("/advanced/restart", HTTP_POST, []() {
    gServer.send(200, "text/html", F("<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='20;url=/'></head><body style='font-family:sans-serif;padding:20px'>"
      "<h3>Restarting.</h3><p>This page returns in a few seconds.</p></body></html>"));
    delay(500);
    ESP.restart();
  });
}
