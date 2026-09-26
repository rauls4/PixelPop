#pragma once
// Wi-Fi: connecting with saved credentials, and the temporary "PixelPop"
// setup network + web page used to enter them.

#include <Arduino.h>

void netLoadCreds();                          // read saved Wi-Fi (and one-shot "setup" flag)
bool netHasCreds();
bool netForcePortal();                        // true if a Wi-Fi reset was requested
void netRequestPortalOnNextBoot();
const String &netHostname();
bool netSetHostname(const String &hostname);   // lower-case letters, digits, and hyphens only

// Start the connection (once) and wait up to waitMs. Safe to call repeatedly.
bool netConnect(unsigned long waitMs = 15000);

// Open the temporary setup network and page. Never returns (restarts the board).
void netRunPortal();
