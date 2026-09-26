#include "CrashLog.h"
#include "Settings.h"

const char *crashLogReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software reset";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "unknown";
  }
}

static void reasonKey(uint8_t slot, char *out, size_t cap) {
  snprintf(out, cap, "crR%u", (unsigned)slot);
}

void crashLogRecord(esp_reset_reason_t reason) {
  if (reason == ESP_RST_POWERON) return;

  Store store("");
  const uint8_t head = store.getU8("crHead", 0) % CRASH_LOG_SIZE;
  const uint8_t count = store.getU8("crCount", 0);
  char key[8];
  reasonKey(head, key, sizeof(key));
  store.putU8(key, (uint8_t)reason);
  store.putU8("crHead", (head + 1) % CRASH_LOG_SIZE);
  store.putU8("crCount", count < CRASH_LOG_SIZE ? count + 1 : CRASH_LOG_SIZE);
}

uint8_t crashLogCount() {
  Store store("");
  const uint8_t count = store.getU8("crCount", 0);
  return count <= CRASH_LOG_SIZE ? count : 0;
}

uint8_t crashLogReasonAt(uint8_t newestIndex) {
  const uint8_t count = crashLogCount();
  if (newestIndex >= count) return ESP_RST_UNKNOWN;

  Store store("");
  const uint8_t head = store.getU8("crHead", 0) % CRASH_LOG_SIZE;
  const uint8_t slot = (head + CRASH_LOG_SIZE - 1 - newestIndex) % CRASH_LOG_SIZE;
  char key[8];
  reasonKey(slot, key, sizeof(key));
  return store.getU8(key, ESP_RST_UNKNOWN);
}

void crashLogClear() {
  Store store("");
  store.putU8("crHead", 0);
  store.putU8("crCount", 0);
}
