#pragma once
// A small persistent history of resets, shown on the Advanced page.

#include <Arduino.h>
#include <esp_system.h>

#define CRASH_LOG_SIZE 8

void        crashLogRecord(esp_reset_reason_t reason);
uint8_t     crashLogCount();
uint8_t     crashLogReasonAt(uint8_t newestIndex);
void        crashLogClear();
const char *crashLogReasonName(esp_reset_reason_t reason);
