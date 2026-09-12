// Configuration kept across power loss (emulated EEPROM)
#pragma once
#include <Arduino.h>

enum TlsMode : uint8_t { TLS_INSECURE = 0, TLS_FINGERPRINT = 1, TLS_CA = 2 };

struct __attribute__((packed)) Settings {
  uint32_t magic;
  char     ssid[33];
  char     pass[65];
  char     serverUrl[128];     // http://host:port or https://host:port
  uint8_t  tlsMode;
  uint8_t  fingerprint[20];
  uint8_t  pollMinutes;        // default 5
  uint8_t  nightStartHour;     // default 0
  uint8_t  nightEndHour;       // default 6
  uint8_t  nightPollMinutes;   // default 30
  uint8_t  nightTopbar;        // 1 = still refresh the top bar every minute at night
  uint8_t  batShowPercent;     // 0 voltage, 1 percentage
  uint8_t  rtcType;            // 0 = BL sequential read, 1 = RX offset read
  uint8_t  rtcTypeKnown;
  int16_t  clockCompMs;        // manual daily compensation in ms (applied when sync fails)
  uint8_t  rotation;           // 0 or 2
  uint8_t  portalEnable;
  char     portalUrl[128];
  char     portalUser[48];
  char     portalPass[48];
  char     portalFields[40];   // "username,password"
  char     portalExtra[96];    // "k=v&k2=v2"
  char     portalOk[31];       // success-detection string
  uint8_t  lang;               // 0 Chinese, 1 English (borrowed from portalOk's last byte, so the struct size and offsets are unchanged and old EEPROM still validates)
  uint8_t  configured;         // 1 = provisioning completed
  uint32_t crc;
};

extern Settings cfg;
void settingsLoad();
void settingsSave();
void settingsDefaults();
bool settingsUrlIsHttps();
