#pragma once
// A spinning ring of dots, shown instead of words while there is nothing to display yet
// (waiting for the first weather, calendar, ... data). Call it about every 70 ms.

#include <Arduino.h>

void displaySpinner(unsigned long nowMs);     // clears the screen, draws one frame, shows it
