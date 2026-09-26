#pragma once
// Small wrapper around Preferences (flash storage). Every module gets its own
// Store with a short key prefix so settings never collide, e.g. prefix "w_"
// stores the weather module's "lat" setting under the key "w_lat".
// NVS key names can be at most 15 characters, so keep prefix + key short.

#include <Arduino.h>
#include <Preferences.h>

// Number of settings that could not be written to flash since the start (storage full), and the last one.
uint32_t    storeFailures();
const char *storeLastFailure();
const char *storeLastReason();          // e.g. ESP_ERR_NVS_NOT_ENOUGH_SPACE

class Store {
 public:
  explicit Store(const char *prefix) : _prefix(prefix) {}

  String   getString(const char *key, const String &def);
  void     putString(const char *key, const String &v);
  uint8_t  getU8(const char *key, uint8_t def);
  void     putU8(const char *key, uint8_t v);
  uint32_t getUInt(const char *key, uint32_t def);
  void     putUInt(const char *key, uint32_t v);
  float    getFloat(const char *key, float def);
  void     putFloat(const char *key, float v);
  bool     getBool(const char *key, bool def);
  bool     putBoolChecked(const char *key, bool v);  // writes and reports whether NVS accepted it
  void     putBool(const char *key, bool v);

 private:
  String _prefix;
  String k(const char *key) const { return _prefix + key; }
};
