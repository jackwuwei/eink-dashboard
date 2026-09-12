// Screen drawing: top bar / dashboard content / detail pages / message pages; partial and full refresh
#pragma once
#include <Arduino.h>
#include "clock.h"
#include "sensors.h"
#include "model.h"

enum Page : uint8_t { PAGE_DASH = 0, PAGE_CLAUDE, PAGE_SERVERS, PAGE_WEATHER, PAGE_CALENDAR, PAGE_DEVICE, PAGE_COUNT };

struct UiStatus {
  uint8_t netStatus;      // meaning of rtc.netStatus
  uint8_t lastJsonHour, lastJsonMinute;
  bool haveJson;
  int8_t rssi; uint32_t freeHeap; uint16_t wakeCount; const char* fwVersion; const char* ip;
  bool showKeysHint;
};

void uiBegin(bool fullRefreshMode);   // display.init; fullRefreshMode=true makes the next refresh a full one
void uiEnd();                          // powerOff
void uiDrawTopBarOnly(const ClockNow& t, const SensorData& sd);     // partial refresh of the top bar
void uiDrawAll(const ClockNow& t, const SensorData& sd, const Dash& d, const UiStatus& st, Page page); // whole screen (uiBegin decides full vs partial)
void uiDrawContentOnly(const Dash& d, const UiStatus& st, Page page);   // partial refresh of the content area
void uiClearStatusGhost();             // flash the status line black/white once to clear the ghost of the key hints
void uiMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr); // full-screen message (provisioning, low battery, ...)
void uiPortalScreen(const char* ssid, const char* pass, const char* ip, const char* status);
