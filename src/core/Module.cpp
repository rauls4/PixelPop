#include "Module.h"
#include "WebUI.h"

Module::Module(const char *id, const char *title, const char *prefix,
               bool defEnabled, uint8_t defSeconds)
    : store(prefix), _id(id), _title(title), _defEnabled(defEnabled),
      _defSeconds(defSeconds), _enabled(defEnabled), _seconds(defSeconds) {}

void Module::load() {
  _enabled = store.getBool("en", _defEnabled);
  uint8_t s = store.getU8("sec", _defSeconds);
  if (s < 2) s = 2;
  if (s > 60) s = 60;
  _seconds = s;
  onLoad();
}

bool Module::setEnabled(bool on) {
  if (!store.putBoolChecked("en", on)) return false;
  if (store.getBool("en", !on) != on) return false;
  _enabled = on;
  return true;
}

void Module::save(WebServer &server) {
  _enabled = server.hasArg("enabled");
  if (server.hasArg("seconds")) {
    _seconds = (uint8_t)uiReadLong(server, "seconds", _seconds, 2, 60);
  }
  store.putBool("en", _enabled);
  store.putU8("sec", _seconds);
  onSave(server);
}

String Module::settingsHtml() {
  String h = "<form method='POST' action='/" + String(_id) + "'>";
  h += uiCheckbox("enabled", enabledLabel(), _enabled);
  if (hasPages()) {
    h += uiNumber("seconds", "Seconds on screen", _seconds, 2, 60);
  }
  h += onSettingsHtml();
  if (showSettingsPreview()) h += uiMatrixPreview(_id);
  h += "<button type='submit'>Save</button></form>";
  return h;
}
