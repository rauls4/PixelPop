#include "NetLock.h"
#include "Module.h"
#include "Config.h"
#include <WiFi.h>
#include "freertos/semphr.h"
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define HAVE_HEAP_CAPS 1
#endif

static SemaphoreHandle_t netMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

// ---------- memory ----------
bool netLockTake(unsigned long waitMs) {
  if (xSemaphoreTake(netMutex(), pdMS_TO_TICKS(waitMs)) != pdTRUE) return false;
  return true;
}

void netLockGive() {
  xSemaphoreGive(netMutex());
}

static MemMark gMarks[16];
static int gMarkN = 0;

void memMark(const char *label) {
  if (gMarkN >= 16) return;
  MemMark &m = gMarks[gMarkN++];
  strncpy(m.label, label, sizeof(m.label) - 1);
  m.label[sizeof(m.label) - 1] = 0;
#ifdef HAVE_HEAP_CAPS
  m.freeK = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  m.blockK = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  m.extraK = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
#else
  m.freeK = m.blockK = m.extraK = 0;
#endif
  Serial.printf("Memory %-18s free %uK  largest block %uK  extra %uK\n", m.label, m.freeK, m.blockK, m.extraK);
}
int memMarkCount() { return gMarkN; }
const MemMark &memMarkAt(int i) { return gMarks[i]; }

SimpleLock::SimpleLock() : _h(xSemaphoreCreateMutex()) {}

bool SimpleLock::take(unsigned long waitMs) {
  return xSemaphoreTake((SemaphoreHandle_t)_h, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void SimpleLock::give() {
  xSemaphoreGive((SemaphoreHandle_t)_h);
}

// ---------- shared network task ----------
static Module *volatile gJobs[16];
static volatile int gJobCount = 0;
static bool gWorkerStarted = false;
static const unsigned long WORKER_BOOT_DELAY_MS = 20000;

static void workerEntry(void *) {
  // TLS setup is CPU- and memory-intensive. Do not compete with Wi-Fi, the
  // display, and the audio probe during the vulnerable part of start-up.
  vTaskDelay(pdMS_TO_TICKS(WORKER_BOOT_DELAY_MS));
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    for (int i = 0; i < gJobCount; i++) gJobs[i]->netTick();
  }
}

void netWorkerAdd(Module *m) {
  if (gJobCount < 16) { gJobs[gJobCount] = m; gJobCount = gJobCount + 1; }
  if (!gWorkerStarted) {
    gWorkerStarted = true;
    if (xTaskCreatePinnedToCore(workerEntry, "netwk", 12288, nullptr, 1, nullptr, APP_TASK_CORE) != pdPASS) {
      gWorkerStarted = false;
      Serial.println("Network task could not start");
    }
  }
}

void netExplain(char *out, size_t cap, int httpCode, const char *host, const char *tlsText) {
  IPAddress ip;
  if (WiFi.hostByName(host, ip) != 1) {
    snprintf(out, cap, "could not look up %s (no answer from the router's DNS)", host);
    return;
  }
#ifdef HAVE_HEAP_CAPS
  unsigned freeK = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  unsigned blockK = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
#else
  unsigned freeK = 0, blockK = 0;
#endif
  snprintf(out, cap, "could not connect (%d) %.30s | memory %uK free, largest block %uK",
           httpCode, tlsText ? tlsText : "", freeK, blockK);
}
