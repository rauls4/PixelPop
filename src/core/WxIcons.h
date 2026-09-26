#pragma once
// Weather pictures shared by the Weather and Forecast modules.
// WMO weather codes come from Open-Meteo.

#include <Arduino.h>

// Short condition text that fits 10 characters ("Pt cloudy", "Showers", ...).
const char *wxConditionText(int code);

// Draw the icon centered at (cx, cy).
//   big = true : about 22 x 22 pixels (single-day pages)
//   big = false: about 13 x 11 pixels (three-day view)
void drawWxIcon(int cx, int cy, int code, bool isDay, bool big);

// Draw a compact degree marker at its top-left corner.
void drawWxDegree(int x, int y, uint16_t color);

// Draw a static, detailed large icon for the portrait Weather and Forecast cards.
void drawWxIconDetailed(int cx, int cy, int code, bool isDay);
