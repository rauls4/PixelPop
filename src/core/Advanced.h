#pragma once
// The Advanced page of the settings site: install a new firmware over Wi-Fi, and see how the board
// is doing (memory, Wi-Fi signal, uptime).
//
// Installing firmware is guarded: the page asks for a 4-digit code that appears only on the panel,
// so it takes someone standing at the display, not just someone on the same Wi-Fi.

#include <WebServer.h>

void advancedRegister(WebServer &server);     // adds /advanced and its actions
