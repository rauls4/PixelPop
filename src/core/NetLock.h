#pragma once
// Two small locks used by the modules that fetch data in background tasks.
//
//  * netLockTake / netLockGive: only one secure (HTTPS) download at a time. Each one needs
//    a lot of memory for the encryption, so the flight tracker and the news ticker take turns.
//  * SimpleLock: a plain mutex, for data shared between a background task and the display.

#include <Arduino.h>

class Module;

// One shared background task does every download (weather, forecast, calendar, news, flights).
// Each of those has a netTick() that the task calls about four times a second; the module checks
// whether it is due and, if so, fetches. Sharing one task instead of one task per module saves
// tens of kilobytes of the board's scarce internal memory, which the secure connections need.
void netWorkerAdd(Module *m);

// Turns a failed connection into something readable: the reason if the name could not be looked
// up, otherwise the error codes and how much memory was free (a secure connection needs a lot).
void netExplain(char *out, size_t cap, int httpCode, const char *host, const char *tlsText);

bool netLockTake(unsigned long waitMs);
void netLockGive();

// Memory checkpoints: where the internal memory goes during start-up (shown on the Advanced page and on the
// serial monitor).
struct MemMark { char label[20]; unsigned freeK, blockK, extraK; };
void memMark(const char *label);
int  memMarkCount();
const MemMark &memMarkAt(int i);

class SimpleLock {
 public:
  SimpleLock();
  bool take(unsigned long waitMs);
  void give();
 private:
  void *_h;
};
