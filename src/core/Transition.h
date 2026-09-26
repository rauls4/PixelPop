#pragma once
// Screen transitions: the effect played when the panel moves from one page to
// the next (crossfade, wipe, slides, dissolve, blinds, circle).
//
// How it works: Display keeps a shadow copy of what is on screen. Before the
// switch, the old picture is copied, the new page is drawn "off screen" (capture
// mode), and then the two are blended frame by frame onto the panel.

#include <Arduino.h>
#include "Module.h"

#define TRANSITION_NONE 0
int         transitionCount();
const char *transitionName(int style);        // for the menu
const char *transitionSpeedName(int speed);   // 0 fast, 1 normal, 2 slow

// Play the effect from what is on screen now to page `sub` of module `m`.
// (style 0 does nothing; the last style, "Random", picks a different effect each time.)
void transitionPlay(uint8_t style, uint8_t speed, Module *m, int sub);
