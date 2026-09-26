#pragma once
// Shared constants for the whole project.

#include <Arduino.h>

#define PANEL_W 64
#define PANEL_H 32

#define MAX_BRIGHTNESS 100           // the brightness slider goes no higher than this
#define BUTTON_PIN 0                 // BOOT button
#define AP_NAME    "PixelPop"        // temporary setup network (open, no password)
#define FW_VERSION "1.1.28"            // shown on the Advanced page
#define HOSTNAME   "pixelpop"        // settings page also at http://pixelpop.local

#define IP_SHOW_MS       20000UL                 // how long Wi-Fi and network details are shown
#define PREVIEW_HOLD_MS  1200UL                  // settings-site hover preview: how long it stays up after the last ping
#define PORTAL_TIMEOUT   (5UL * 60 * 1000)       // setup network stays up 5 min, then Wi-Fi is retried
#define WIFI_RETRY_MS    (60UL * 1000)           // check Wi-Fi recovery once a minute while disconnected
#define APP_TASK_CORE    1                       // reserve core 0 for ESP32 Wi-Fi, TCP/IP, and idle tasks
