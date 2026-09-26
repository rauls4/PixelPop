#pragma once
// Sleep screen: turn the display off (or down to a dim clock) by hand or on a weekly schedule.
//
// The schedule only starts working once the clock has been set from the internet. A manual choice
// ("sleep now" / "wake now", or a tap on the BOOT button) holds until the schedule next changes
// its mind, so a display put to sleep at 3 pm stays asleep until the next scheduled wake-up.

#include <Arduino.h>
#include <WebServer.h>

void   sleepBegin();                        // load the saved settings (call once at startup)
bool   sleepTick(bool urgent, bool &woke);  // call every loop; true = the panel must stay dark/dim, skip drawing
bool   sleepIsAsleep();
void   sleepWake();                         // wake now (a tap on the button)
void   sleepRegister(WebServer &server);    // adds /sleep and /sleep/now
String sleepStatus();                       // one line for the settings pages
