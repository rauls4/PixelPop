#pragma once
// A tiny hand-off between the BOOT button and the urgent notice: while an alert is showing, a tap
// on the button dismisses it instead of showing the IP address.

#include <Arduino.h>

bool alertActive();                 // an urgent notice is on the panel
void alertSetActive(bool on);       // the urgent notice module sets this
void alertRequestDismiss();         // the button (or the web page) asks it to stop
bool alertDismissRequested();
void alertClearDismiss();
