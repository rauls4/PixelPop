#include "Net.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Config.h"
#include "Settings.h"
#include "Display.h"
#include "WebUI.h"
#include "LogoAssets.h"

// Wi-Fi credentials live in the same flash namespace as everything else, under
// the un-prefixed keys "ssid", "pass" and "force".
static Store creds("");
static String cfgSsid, cfgPass, cfgHostname;
static bool   forcePortal = false;
static bool   wifiStarted = false;

// The setup portal uses its own web server object (only one is ever running).
static WebServer portalServer(80);
static DNSServer dnsServer;
static String    networkOptions;

// Optional compile-time defaults. Leave empty to use the setup page.
static const char *DEFAULT_SSID = "";
static const char *DEFAULT_PASS = "";

void netLoadCreds() {
  cfgSsid = creds.getString("ssid", DEFAULT_SSID);
  cfgPass = creds.getString("pass", DEFAULT_PASS);
  forcePortal = creds.getBool("force", false);
  cfgHostname = creds.getString("host", HOSTNAME);
  if (!netSetHostname(cfgHostname)) {
    cfgHostname = HOSTNAME;
    creds.putString("host", cfgHostname);
  }
  if (forcePortal) creds.putBool("force", false);     // one-shot
}

bool netHasCreds() { return cfgSsid.length() > 0; }
bool netForcePortal() { return forcePortal; }
void netRequestPortalOnNextBoot() { creds.putBool("force", true); }
const String &netHostname() { return cfgHostname; }

bool netSetHostname(const String &hostname) {
  String value = hostname;
  value.trim();
  value.toLowerCase();
  if (value.length() < 1 || value.length() > 24 || value[0] == '-' || value[value.length() - 1] == '-') return false;
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  cfgHostname = value;
  creds.putString("host", cfgHostname);
  return true;
}

static const char *wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_CONNECTED:      return "connected";
    case WL_NO_SSID_AVAIL:  return "network name not found (check SSID; 2.4 GHz only)";
    case WL_CONNECT_FAILED: return "connect failed (check password)";
    case WL_DISCONNECTED:   return "disconnected / still trying";
    case WL_IDLE_STATUS:    return "idle / connecting";
    default:                return "unknown";
  }
}

// Call WiFi.begin() only ONCE. Calling it again while a connection attempt is
// in progress causes "sta is connecting, cannot set config". After the first
// begin(), the ESP32 keeps retrying on its own (auto-reconnect), so later calls
// just wait for it.
bool netConnect(unsigned long waitMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  if (!wifiStarted) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfgHostname.c_str());
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    wifiStarted = true;
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < waitMs) delay(250);

  Serial.print("WiFi: ");
  Serial.println(wifiStatusText(WiFi.status()));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  return false;
}

// ---------- Setup portal ----------
static void handlePortalRoot() {
  String page = uiHead("PixelPop Wi-Fi Setup");
  page += F("<div class='card'><div class='brand'><img class='wordmark' src='/mark.png' width='320' height='120' alt='PixelPop'></div><h2>Wi-Fi setup</h2>"
    "<p>Choose your <b>2.4 GHz</b> Wi-Fi network and enter its password.</p>"
    "<form method='POST' action='/save'>"
    "<label for='ssid'>Network name</label>"
    "<input id='ssid' name='ssid' type='text' list='nets' maxlength='32' required "
    "autocapitalize='none' autocorrect='off' spellcheck='false'><datalist id='nets'>");
  page += networkOptions;
  page += F("</datalist><label for='p'>Password</label>"
    "<input id='p' name='pass' type='password' maxlength='63' autocapitalize='none'>"
    "<label class='s'><input type='checkbox' onclick=\"document.getElementById('p').type=this.checked?'text':'password'\"> Show password</label>"
    "<button type='submit'>Save &amp; connect</button></form></div></body></html>");
  portalServer.send(200, "text/html", page);
}

static void handlePortalSave() {
  String ssid = portalServer.arg("ssid");
  String pass = portalServer.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63) {
    portalServer.send(400, "text/html", F("<html><body style='font-family:sans-serif;padding:20px'>"
      "<h3>That did not look right.</h3><p><a href='/'>Go back</a> and try again.</p></body></html>"));
    return;
  }
  creds.putString("ssid", ssid);
  creds.putString("pass", pass);
  creds.putBool("force", false);
  portalServer.send(200, "text/html", F("<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
    "<body style='font-family:sans-serif;padding:20px'><h3>Saved.</h3>"
    "<p>The matrix is restarting and will join your network. Watch the panel: it will show "
    "its network details for 20 seconds once connected. You can reconnect your phone to your normal Wi-Fi.</p></body></html>"));
  delay(1500);
  ESP.restart();
}

// Any other URL (including the "is there internet?" checks phones make)
// redirects to the setup page, which makes the setup page pop up automatically.
static void handlePortalRedirect() {
  portalServer.sendHeader("Location", "http://192.168.4.1/", true);
  portalServer.send(302, "text/plain", "");
}

static void drawWifiSetupIcon() {
  const int W = gDisplay->width(), H = gDisplay->height();
  const int cx = W / 2;
  const int cy = H / 2 + min(6, H / 6);
  const int outer = min(W / 2 - 4, H / 3);
  gDisplay->fillScreen(0);

  for (int ring = 0; ring < 3; ring++) {
    const int radius = outer - ring * 4;
    if (radius < 3) continue;
    for (int y = cy - radius; y <= cy; y++) {
      for (int x = cx - radius; x <= cx + radius; x++) {
        const int dx = x - cx, dy = y - cy;
        const int distance2 = dx * dx + dy * dy;
        const int inner = radius - 2;
        if (distance2 >= inner * inner && distance2 <= radius * radius) gDisplay->drawPixel(x, y, COL_WHITE);
      }
    }
  }
  gDisplay->fillCircle(cx, cy + 3, 2, COL_WHITE);
  gDisplay->flipDMABuffer();
}

static void drawPortalScreen(int which) {
  if (which == 0) drawWifiSetupIcon();
  else            drawLines("Then open", "192.168", ".4.1");
}

void netRunPortal() {
  Serial.println("Starting setup network...");
  drawLines("Scanning", "WiFi...");

  // Scan for nearby networks so the page can offer them as suggestions
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(200);
  int n = WiFi.scanNetworks();
  networkOptions = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    String opt = "<option value=\"" + htmlEscape(s) + "\">";
    if (networkOptions.indexOf(opt) < 0) networkOptions += opt;
  }
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME);            // open network, no password (temporary)
  delay(300);
  dnsServer.start(53, "*", WiFi.softAPIP());

  portalServer.on("/", HTTP_GET, handlePortalRoot);
  portalServer.on("/mark.png", HTTP_GET, []() {
    portalServer.sendHeader("Cache-Control", "max-age=86400");
    portalServer.send_P(200, "image/png", (PGM_P)LOGO_MARK_PNG, LOGO_MARK_PNG_LEN);
  });
  portalServer.on("/icon.png", HTTP_GET, []() {
    portalServer.sendHeader("Cache-Control", "max-age=86400");
    portalServer.send_P(200, "image/png", (PGM_P)LOGO_ICON_PNG, LOGO_ICON_PNG_LEN);
  });
  portalServer.on("/save", HTTP_POST, handlePortalSave);
  portalServer.onNotFound(handlePortalRedirect);
  portalServer.begin();

  Serial.print("Setup network: ");
  Serial.print(AP_NAME);
  Serial.print("  page: http://");
  Serial.println(WiFi.softAPIP());

  unsigned long start = millis(), lastScreen = 0;
  int screen = 0;
  while (millis() - start < PORTAL_TIMEOUT) {
    dnsServer.processNextRequest();
    portalServer.handleClient();
    if (millis() - lastScreen > 3000 || lastScreen == 0) {
      drawPortalScreen(screen++ % 2);
      lastScreen = millis();
    }
    delay(2);
  }
  Serial.println("Setup timed out, restarting to retry Wi-Fi");
  ESP.restart();
  while (true) delay(1000);
}
