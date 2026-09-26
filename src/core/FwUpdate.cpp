#include "FwUpdate.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include "NetLock.h"
#include "App.h"
#include "Config.h"
#include "Display.h"

// ---------- the version carried inside the firmware ----------
// "@@PXP:" plus the version, kept in the program so a downloaded file can be searched for it.
static const char kMark[] = "@@PXP:" FW_VERSION;
const char *fwVersionText() { return kMark + 6; }

// Finds "@@PXP:<digits and dots>" in a stream of bytes, whatever the block boundaries.
struct VerScan {
  uint8_t m = 0;               // how much of the prefix has matched
  bool    collecting = false;
  bool    done = false;
  uint8_t n = 0;
  char    v[16] = "";
  void feed(const uint8_t *b, size_t len) {
    static const char P[] = "@@PXP:";
    for (size_t i = 0; i < len && !done; i++) {
      const uint8_t c = b[i];
      if (collecting) {
        if ((c >= '0' && c <= '9') || c == '.') { if (n < sizeof(v) - 1) v[n++] = (char)c; continue; }
        if (n > 0) { v[n] = 0; done = true; return; }
        collecting = false; m = 0;                   // the prefix on its own (this program's own search text)
      }
      if (c == (uint8_t)P[m]) {
        if (P[++m] == 0) { collecting = true; n = 0; m = 0; }
      } else {
        m = (c == (uint8_t)P[0]) ? 1 : 0;
      }
    }
  }
};

// ---------- checks ----------
const char *fwCheckHeader(const uint8_t *b, size_t n) {
  if (n < 36) return "That file is too short to be a firmware.";
  if (b[0] != 0xE9) return "That is not a firmware file. Use the .ino.bin file from Export Compiled Binary.";
  if (!(b[12] == 0x09 && b[13] == 0x00)) return "That firmware is for a different chip.";
  // A real program has its description block right after the first segment header (magic 0xABCD5432).
  // Bootloader and "merged" files start like a program but lack it, and would leave the display unable to start.
  if (!(b[32] == 0x32 && b[33] == 0x54 && b[34] == 0xCD && b[35] == 0xAB))
    return "That file is not the program itself (it looks like a bootloader or merged file). Use the file ending in .ino.bin.";
  return nullptr;
}

// ---------- the saved address ----------
String fwSavedUrl() {
  Preferences p; p.begin("fwup", true);
  String u = p.getString("url", "");
  p.end();
  return u;
}

void fwSaveUrl(const String &url) {
  Preferences p; p.begin("fwup", false);
  p.putString("url", url);
  p.end();
}

const char *fwCleanUrl(String &url) {
  url.trim();
  if (url.length() == 0) return "Enter the address of the firmware file first.";
  String low = url; low.toLowerCase();
  if (!low.startsWith("https://")) return "The address must start with https://";
  if (url.length() > 300) return "That address is too long.";
  for (unsigned i = 0; i < url.length(); i++) {
    const char c = url[i];
    if (c <= ' ' || c == '"' || c == '\'' || c == '<' || c == '>') return "That address has characters that cannot be used in a web address.";
  }
  return nullptr;
}

static String hostOf(const String &u) {
  int a = u.indexOf("://"); a = a < 0 ? 0 : a + 3;
  int b = u.indexOf('/', a);
  return u.substring(a, b < 0 ? (unsigned)u.length() : (unsigned)b);
}

static void explainCode(char *out, size_t cap, int code, WiFiClientSecure &c, const String &url) {
  if (code > 0) {
    snprintf(out, cap, "The server answered %d%s.", code,
             code == 404 ? " (file not found: check the address)" : code == 403 ? " (not allowed: the link may have expired)" : "");
    return;
  }
  char tls[64] = ""; c.lastError(tls, sizeof(tls));
  netExplain(out, cap, code, hostOf(url).c_str(), tls);
}

static String mbText(uint32_t bytes) { return String((float)bytes / 1048576.0f, 1) + " MB"; }

// "size|tag", where the tag is the ETag if the server sends one, else the modified date.
static String makeMark(const String &url, long size, const String &etag, const String &modified) {
  return url + "\n" + String(size) + "|" + (etag.length() ? etag : modified);
}

static String installedMark() {
  Preferences p; p.begin("fwup", true);
  String m = p.getString("mark", "");
  p.end();
  return m;
}

// ---------- status shared with the download task ----------
static volatile FwState gState = FW_IDLE;
static FwStatus gSt = {FW_IDLE, 0, 0, 0, ""};
static String gUrl, gMd5;
static char gInstalledVersion[16] = "";
static const char *const AUTO_URL = "https://raw.githubusercontent.com/rauls4/PixelPop/main/PixelPop.ino.bin";
static const unsigned long AUTO_CHECK_MS = 15UL * 60UL * 1000;
static volatile bool gAutoChecking = false;
static unsigned long gAutoLastCheck = 0;
static char gAutoStatus[128] = "";

FwStatus fwStatus() {
  FwStatus s = gSt;
  s.state = gState;
  return s;
}

static void setMsg(const char *t) { snprintf(gSt.msg, sizeof(gSt.msg), "%s", t); }

// ---------- look at the file without downloading it ----------
String fwCheck(const String &url, bool &ok) {
  ok = false;
  if (gState == FW_RUNNING) return "An update is already running.";
  if (!netLockTake(20000)) return "The display is busy with other downloads. Try again in a moment.";
  String out;
  {
    WiFiClientSecure client; client.setInsecure(); client.setTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(10000); http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("PixelPop/" FW_VERSION);
    const char *keys[] = {"ETag", "Last-Modified"};
    http.collectHeaders(keys, 2);
    if (!http.begin(client, url)) {
      out = "That address could not be used.";
    } else {
      const int code = http.GET();
      if (code == 200) {
        const long size = http.getSize();
        const String etag = http.header("ETag"), modified = http.header("Last-Modified");
        if (size <= 0) {
          out = "The server did not say how big the file is, so it cannot be downloaded to the display. Use a direct link to the .bin file.";
        } else if (size < 65536) {
          out = "That file is only " + String(size) + " bytes, too small to be firmware. It may be a web page: check the address.";
        } else {
          const esp_partition_t *nxt = esp_ota_get_next_update_partition(nullptr);
          if (nxt && (uint32_t)size > nxt->size) {
            out = "The file (" + mbText(size) + ") is bigger than the update area (" + mbText(nxt->size) + ").";
          } else {
            ok = true;
            out = "Found a file of " + mbText(size);
            if (modified.length()) out += ", last modified " + modified;
            out += ". ";
            const String saved = installedMark();
            const String now = makeMark(url, size, etag, modified);
            if (saved.length() == 0 || !saved.startsWith(url.c_str()))
              out += "Nothing has been installed from this address yet, so the display cannot tell whether it is newer. Install it if you expect an update.";
            else if (saved == now)
              out += "It is the same file you installed last time. You are up to date.";
            else
              out += "It is different from the one you installed last time, so an update is available.";
          }
        }
      } else {
        char why[128]; explainCode(why, sizeof(why), code, client, url);
        out = why;
      }
      http.end();
    }
  }
  netLockGive();
  return out;
}

// ---------- download and install ----------
static bool download(char *why, size_t cap) {
  if (!netLockTake(30000)) { snprintf(why, cap, "The display is busy with other downloads. Try again in a moment."); return false; }
  bool ok = false;
  uint8_t *buf = nullptr;
  {
    WiFiClientSecure client; client.setInsecure(); client.setTimeout(20);
    HTTPClient http;
    http.setConnectTimeout(10000); http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("PixelPop/" FW_VERSION);
    const char *keys[] = {"ETag", "Last-Modified"};
    http.collectHeaders(keys, 2);
    do {
      if (!http.begin(client, gUrl)) { snprintf(why, cap, "That address could not be used."); break; }
      const int code = http.GET();
      if (code != 200) { explainCode(why, cap, code, client, gUrl); break; }
      const long total = http.getSize();
      const String etag = http.header("ETag"), modified = http.header("Last-Modified");
      if (total <= 0) { snprintf(why, cap, "The server did not say how big the file is. Use a direct link to the .bin file."); break; }
      if (total < 65536) { snprintf(why, cap, "That file is only %ld bytes, too small to be firmware. It may be a web page: check the address.", total); break; }
      const esp_partition_t *nxt = esp_ota_get_next_update_partition(nullptr);
      if (!nxt) { snprintf(why, cap, "This display has no second program area, so it cannot install an update itself."); break; }
      if ((uint32_t)total > nxt->size) { snprintf(why, cap, "The file (%.1f MB) is bigger than the update area (%.1f MB).", total / 1048576.0f, nxt->size / 1048576.0f); break; }
      if (!Update.begin((size_t)total)) { snprintf(why, cap, "%s", Update.errorString()); break; }
      if (gMd5.length() == 32) Update.setMD5(gMd5.c_str());
      buf = (uint8_t *)malloc(4096);
      if (!buf) { Update.abort(); snprintf(why, cap, "Not enough free memory to start the download."); break; }

      gSt.total = (uint32_t)total;
      WiFiClient *s = http.getStreamPtr();
      long got = 0;
      unsigned long lastData = millis();
      uint8_t head[40]; size_t hn = 0; bool headChecked = false;
      VerScan vs;
      bool bad = false;
      while (got < total) {
        const int av = s->available();
        if (av <= 0) {
          if (!http.connected()) break;                       // the server hung up
          if (millis() - lastData > 20000) break;             // nothing for 20 seconds
          delay(2);
          continue;
        }
        int n = s->readBytes(buf, av < 4096 ? av : 4096);
        if (n <= 0) { delay(1); continue; }
        lastData = millis();
        if ((long)n > total - got) n = (int)(total - got);
        if (!headChecked) {                                    // judge the file from its first bytes
          for (int i = 0; i < n && hn < sizeof(head); i++) head[hn++] = buf[i];
          if (hn >= 36) {
            headChecked = true;
            const char *h = fwCheckHeader(head, hn);
            if (h) { snprintf(why, cap, "%s", h); bad = true; break; }
          }
        }
        if (Update.write(buf, (size_t)n) != (size_t)n) { snprintf(why, cap, "%s", Update.errorString()); bad = true; break; }
        vs.feed(buf, (size_t)n);
        got += n;
        gSt.bytes = (uint32_t)got;
        gSt.pct = (uint8_t)((uint64_t)got * 100 / (uint64_t)total);
        delay(1);
      }
      if (bad) { Update.abort(); break; }
      if (got < total) {
        Update.abort();
        snprintf(why, cap, "The download stopped early (%ld of %ld KB). Try again.", got / 1024, total / 1024);
        break;
      }
      if (!Update.end(true)) {
        const char *e = Update.errorString();
        if (gMd5.length() == 32 && strstr(e, "MD5")) snprintf(why, cap, "The file does not match the checksum you entered, so it was not installed.");
        else snprintf(why, cap, "%s", e);
        break;
      }
      if (esp_ota_set_boot_partition(nxt) != ESP_OK) {
        snprintf(why, cap, "The update was written, but the next firmware partition could not be selected.");
        break;
      }
      const esp_partition_t *boot = esp_ota_get_boot_partition();
      if (!boot || boot->address != nxt->address) {
        snprintf(why, cap, "The update was written, but the display did not retain the new boot partition.");
        break;
      }
      ok = true;
      if (vs.done) snprintf(gInstalledVersion, sizeof(gInstalledVersion), "v%s", vs.v);
      else         snprintf(gInstalledVersion, sizeof(gInstalledVersion), "installed");
      { Preferences p; p.begin("fwup", false); p.putString("mark", makeMark(gUrl, total, etag, modified)); p.end(); }
      if (vs.done) snprintf(why, cap, "Installed version %s. The display is restarting.", vs.v);
      else         snprintf(why, cap, "Installed. The display is restarting.");
    } while (0);
    http.end();
  }
  if (buf) free(buf);
  netLockGive();
  return ok;
}

static void fetchTask(void *) {
  char why[128] = "";
  const bool ok = download(why, sizeof(why));
  setMsg(why);
  if (ok) {
    gState = FW_DONE;
    vTaskDelay(pdMS_TO_TICKS(2500));                          // time for the page to see the result
    displayBlackout();
    vTaskDelay(pdMS_TO_TICKS(80));
    ESP.restart();
  } else {
    gState = FW_FAILED;
  }
  vTaskDelete(nullptr);
}

bool fwStart(const String &url, const String &md5, String &why) {
  if (gState == FW_RUNNING) { why = "An update is already running."; return false; }
  gUrl = url;
  gMd5 = md5; gMd5.trim(); gMd5.toLowerCase();
  gInstalledVersion[0] = 0;
  if (gMd5.length() != 0 && gMd5.length() != 32) { why = "The checksum must be 32 characters, or left empty."; return false; }
  gSt.bytes = 0; gSt.total = 0; gSt.pct = 0;
  setMsg("Connecting");
  gState = FW_RUNNING;
  app.showFirmwareProgress(0);
  if (xTaskCreatePinnedToCore(fetchTask, "fwget", 12288, nullptr, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    gState = FW_IDLE;
    why = "Not enough free memory to start the download.";
    return false;
  }
  return true;
}

// ---------- Home page: offer, do not auto-install ----------
// Reuses fwCheck's byte-level comparison against the fixed official URL. That comparison cannot
// tell "current" from "never installed over Wi-Fi" apart on bytes alone, so this only answers true
// for the one case fwCheck can state with confidence: a change from what this exact address
// installed last time. Keep this substring in sync with the wording in fwCheck() above.
bool fwCheckOfficial(String &msg) {
  bool ok = false;
  msg = fwCheck(AUTO_URL, ok);
  return ok && msg.indexOf("different from the one you installed") >= 0;
}

bool fwStartOfficial(String &why) { return fwStart(AUTO_URL, "", why); }

bool fwAutoEnabled() {
  Preferences p;
  p.begin("fwup", true);
  const bool enabled = p.getBool("auto", false);
  p.end();
  return enabled;
}

void fwAutoSetEnabled(bool enabled) {
  Preferences p;
  p.begin("fwup", false);
  p.putBool("auto", enabled);
  p.end();
  gAutoLastCheck = 0;
  snprintf(gAutoStatus, sizeof(gAutoStatus), "%s", enabled ? "Waiting to check official firmware." : "Automatic updates are off.");
}

String fwAutoStatus() {
  return String(gAutoStatus);
}

static void autoCheckTask(void *) {
  bool newer = false;
  const String result = fwCheck(AUTO_URL, newer);
  if (!newer) {
    snprintf(gAutoStatus, sizeof(gAutoStatus), "%s", result.c_str());
  } else {
    String why;
    // Disable before the OTA begins. If the server changes headers between the
    // check and reboot, or the saved marker cannot be read after restart, the
    // display must never re-enter an install/reboot loop.
    fwAutoSetEnabled(false);
    if (fwStart(AUTO_URL, "", why)) {
      snprintf(gAutoStatus, sizeof(gAutoStatus), "Downloading official update; automatic updates turn off after this install.");
    } else {
      snprintf(gAutoStatus, sizeof(gAutoStatus), "%s", why.c_str());
    }
  }
  gAutoChecking = false;
  vTaskDelete(nullptr);
}

bool fwAutoCheckNow() {
  if (!fwAutoEnabled()) return false;
  if (gAutoChecking || gState == FW_RUNNING) return false;
  gAutoChecking = true;
  gAutoLastCheck = millis();
  snprintf(gAutoStatus, sizeof(gAutoStatus), "Checking official firmware.");
  if (xTaskCreatePinnedToCore(autoCheckTask, "fwcheck", 10240, nullptr, 1, nullptr, APP_TASK_CORE) != pdPASS) {
    gAutoChecking = false;
    snprintf(gAutoStatus, sizeof(gAutoStatus), "Not enough memory to check for updates.");
    return false;
  }
  return true;
}

void fwAutoTick() {
  if (!fwAutoEnabled() || gAutoChecking || gState == FW_RUNNING || WiFi.status() != WL_CONNECTED) return;
  if (gAutoLastCheck != 0 && millis() - gAutoLastCheck < AUTO_CHECK_MS) return;
  fwAutoCheckNow();
}

// ---------- the panel ----------
void fwPanelTick() {
  static FwState shown = FW_IDLE;
  static unsigned long last = 0;
  const FwState s = gState;
  if (s == FW_RUNNING) {
    if (millis() - last < 100) return;
    last = millis();
    app.showFirmwareProgress(gSt.pct);
    shown = s;
  } else if (s != shown) {
    shown = s;
    if (s == FW_DONE)        app.showMessage("UPDATE", gInstalledVersion, "RESTART", 600000);
    else if (s == FW_FAILED) app.showMessage("Update", "failed", nullptr, 6000);
  }
  fwAutoTick();
}
