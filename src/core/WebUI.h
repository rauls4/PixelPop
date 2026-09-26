#pragma once
// The settings web site served by the board: a main page with an on/off
// toggle for every function, plus one customization page per function.
// This header also has the small HTML helpers modules use to build their forms.

#include <Arduino.h>
#include <WebServer.h>

extern WebServer gServer;

void webBegin();      // register routes and start serving (call after modules are added)
void webLoop();       // handle requests (call often)
void webRedirect(const char *where);

// ---- HTML helpers for module settings forms ----
String htmlEscape(const String &s);
String uiHead(const char *title);                       // doctype + styles + <body>
String uiSection(const char *title);
String uiCheckbox(const char *name, const char *label, bool on);
String uiNumber(const char *name, const char *label, long value, long lo, long hi);
String uiText(const char *name, const char *label, const String &value, int maxLen);
String uiDate(const char *name, const char *label, const String &value);   // YYYY-MM-DD
String uiColor(const char *name, const char *label, uint32_t rrggbb);
String uiSelect(const char *name, const char *label, const char *const *options, int count, int selected);
String uiMatrixPreview(const char *moduleId);

// ---- reading submitted forms ----
bool uiReadColor(WebServer &s, const char *name, uint32_t &out);
long uiReadLong(WebServer &s, const char *name, long def, long lo, long hi);
