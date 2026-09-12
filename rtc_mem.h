// State kept across deep sleep (ESP8266 RTC user memory, 512 B)
#pragma once
#include <Arduino.h>

struct __attribute__((packed)) RtcState {
  uint32_t magic;
  // Time (software-clock backup, used when the chip is unavailable)
  uint8_t  hour, minute, second;
  uint8_t  rtcChipOk;        // 1 = clock chip reading is valid
  uint16_t year; uint8_t month, day;
  uint32_t lastRunMs;        // runtime of the previous wakeup
  // Counters
  uint16_t wakeCount;
  uint8_t  partialCount;     // partial-refresh count (for statistics)
  uint8_t  wifiFail;
  uint8_t  portalFail;
  uint8_t  netFail;
  uint8_t  shtFail;
  uint8_t  ntpDoneDay;       // whether NTP already ran today (stores the day)
  // WiFi fast-connect cache
  uint8_t  bssid[6];
  uint8_t  channel;
  uint8_t  wifiCacheOk;
  uint8_t  phyMode;          // 0 = 11g (default; some ASUS WiFi 6/7 routers only accept this), 1 = 11n; switched automatically and remembered on failure
  uint32_t ip, gw, mask, dns;
  // last values shown in the top bar (decides whether a partial refresh is needed)
  int16_t  lastTemp10, lastHumi10;
  uint8_t  lastBatBars, lastCharging, lastBatPct;
  // Voltage history (charging inference)
  uint16_t vHist[5];         // mV (only trusted samples taken with RF on)
  uint8_t  vIdx;
  uint16_t lastVbatMv;       // last trusted voltage, reused by RF-off wakeups
  int8_t   lastRssi;         // signal strength at the last connection
  // Server
  char     rev[12];
  uint16_t pollMinutes;      // poll interval pushed by the server (minutes)
  uint8_t  lastJsonOk;       // whether the last connection succeeded
  uint8_t  lastJsonHour, lastJsonMinute;
  uint8_t  alert;
  uint8_t  wxCode;           // current weather code (Seniverse encoding, 99 = none), used by the top-bar weather line
  char     wxText[14];       // weather text, e.g. "Cloudy"
  int8_t   wxLo, wxHi;       // today's low/high, -99 = missing
  uint8_t  netStatus;        // 0 ok 1 wifi fail 2 portal 3 server offline 4 cert err
  uint8_t  contentDirty;     // content area needs a redraw (e.g. after a failure)
  uint8_t  page;             // currently displayed page
  uint8_t  forceFull;        // force a full refresh on the next wakeup
  // TLS session cache (br_ssl_session_parameters ~ 88 B)
  uint8_t  tlsSession[96];
  uint8_t  tlsSessionOk;
  uint32_t tlsSessionTs;
  uint32_t crc;
};

extern RtcState rtc;
bool rtcLoad();     // false = invalid (first boot)
void rtcSave();
void rtcReset();
