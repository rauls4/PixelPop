#include "Alert.h"

static volatile bool sActive = false;
static volatile bool sDismiss = false;

bool alertActive() { return sActive; }
void alertSetActive(bool on) { sActive = on; if (on) sDismiss = false; }
void alertRequestDismiss() { sDismiss = true; }
bool alertDismissRequested() { return sDismiss; }
void alertClearDismiss() { sDismiss = false; }
