#include "Settings.h"

#if __has_include(<nvs.h>)
#include <nvs.h>
#include <nvs_flash.h>
#define STORE_RAW_NVS 1
#endif

#define STORE_NS "wxcfg"

// Every write is checked: when the flash storage is full, Preferences quietly refuses the value and the
// setting comes back as it was after the next restart. The failures are counted and shown on the
// Advanced page, and printed on the serial monitor.
static uint32_t gFails = 0;
static char     gLastFail[24] = "";
static char     gLastWhy[32] = "";

static void noteFail(const String &key, const char *why = "") {
  gFails++;
  strncpy(gLastFail, key.c_str(), sizeof(gLastFail) - 1);
  gLastFail[sizeof(gLastFail) - 1] = 0;
  strncpy(gLastWhy, why, sizeof(gLastWhy) - 1);
  gLastWhy[sizeof(gLastWhy) - 1] = 0;
  Serial.print("Settings: could not save ");
  Serial.print(key);
  Serial.print(" (");
  Serial.print(why);
  Serial.println(")");
}

#ifdef STORE_RAW_NVS
// Writes go straight to the flash storage so the reason for a refusal is known (storage full, value too
// long, out of memory ...). Reads still use Preferences; both use the same namespace and value types.
template <class F> static bool nvsPut(const String &key, F fn) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(STORE_NS, NVS_READWRITE, &h);
  if (e == ESP_ERR_NVS_NOT_INITIALIZED) { nvs_flash_init(); e = nvs_open(STORE_NS, NVS_READWRITE, &h); }
  if (e == ESP_OK) {
    e = fn(h, key.c_str());
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
  }
  if (e != ESP_OK) { noteFail(key, esp_err_to_name(e)); return false; }
  return true;
}
#endif

uint32_t    storeFailures() { return gFails; }
const char *storeLastFailure() { return gLastFail; }
const char *storeLastReason() { return gLastWhy; }

String Store::getString(const char *key, const String &def) {
  Preferences p;
  p.begin(STORE_NS, false);
  String v = p.getString(k(key).c_str(), def);
  p.end();
  return v;
}

void Store::putString(const char *key, const String &v) {
#ifdef STORE_RAW_NVS
  nvsPut(k(key), [&](nvs_handle_t h, const char *kk) { return nvs_set_str(h, kk, v.c_str()); });
  return;
#endif
  Preferences p;
  if (!p.begin(STORE_NS, false)) { noteFail(k(key)); return; }
  size_t n = p.putString(k(key).c_str(), v);
  p.end();
  if (n == 0 && v.length() > 0) noteFail(k(key));
}

uint8_t Store::getU8(const char *key, uint8_t def) {
  Preferences p;
  p.begin(STORE_NS, false);
  uint8_t v = p.getUChar(k(key).c_str(), def);
  p.end();
  return v;
}

void Store::putU8(const char *key, uint8_t v) {
#ifdef STORE_RAW_NVS
  nvsPut(k(key), [&](nvs_handle_t h, const char *kk) { return nvs_set_u8(h, kk, v); });
  return;
#endif
  Preferences p;
  if (!p.begin(STORE_NS, false)) { noteFail(k(key)); return; }
  size_t n = p.putUChar(k(key).c_str(), v);
  p.end();
  if (n == 0) noteFail(k(key));
}

uint32_t Store::getUInt(const char *key, uint32_t def) {
  Preferences p;
  p.begin(STORE_NS, false);
  uint32_t v = p.getUInt(k(key).c_str(), def);
  p.end();
  return v;
}

void Store::putUInt(const char *key, uint32_t v) {
#ifdef STORE_RAW_NVS
  nvsPut(k(key), [&](nvs_handle_t h, const char *kk) { return nvs_set_u32(h, kk, v); });
  return;
#endif
  Preferences p;
  if (!p.begin(STORE_NS, false)) { noteFail(k(key)); return; }
  size_t n = p.putUInt(k(key).c_str(), v);
  p.end();
  if (n == 0) noteFail(k(key));
}

float Store::getFloat(const char *key, float def) {
  Preferences p;
  p.begin(STORE_NS, false);
  float v = p.getFloat(k(key).c_str(), def);
  p.end();
  return v;
}

void Store::putFloat(const char *key, float v) {
#ifdef STORE_RAW_NVS
  nvsPut(k(key), [&](nvs_handle_t h, const char *kk) { return nvs_set_blob(h, kk, &v, sizeof(v)); });
  return;
#endif
  Preferences p;
  if (!p.begin(STORE_NS, false)) { noteFail(k(key)); return; }
  size_t n = p.putFloat(k(key).c_str(), v);
  p.end();
  if (n == 0) noteFail(k(key));
}

bool Store::getBool(const char *key, bool def) {
  Preferences p;
  p.begin(STORE_NS, false);
  bool v = p.getBool(k(key).c_str(), def);
  p.end();
  return v;
}

bool Store::putBoolChecked(const char *key, bool v) {
#ifdef STORE_RAW_NVS
  return nvsPut(k(key), [&](nvs_handle_t h, const char *kk) { return nvs_set_u8(h, kk, v ? 1 : 0); });
#endif
  Preferences p;
  if (!p.begin(STORE_NS, false)) { noteFail(k(key)); return false; }
  size_t n = p.putBool(k(key).c_str(), v);
  p.end();
  if (n == 0) noteFail(k(key));
  return n != 0;
}

void Store::putBool(const char *key, bool v) {
  putBoolChecked(key, v);
}
