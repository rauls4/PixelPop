#include "I2cBus.h"
#include "NetLock.h"

static SimpleLock &busLock() {
  static SimpleLock lock;              // created on first use
  return lock;
}

bool i2cLock(unsigned long waitMs) { return busLock().take(waitMs); }
void i2cUnlock() { busLock().give(); }
