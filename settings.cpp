#include "settings.h"
#include "config.h"
#include "i18n.h"
#include <ESP_EEPROM.h>

Settings cfg;
static const uint32_t CFG_MAGIC = 0x48444331; // "HDC1"
// EEPROM layout lock: lang was borrowed from the tail of portalOk, so neither the struct size nor any field
// offset may change, otherwise configs on older devices are treated as invalid
static_assert(sizeof(Settings) == 660, "Settings layout changed: old EEPROM contents would be discarded");

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}

void settingsDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strcpy(cfg.serverUrl, "http://192.168.1.100:8090");
  cfg.tlsMode = TLS_INSECURE;
  cfg.pollMinutes = 5;
  cfg.nightStartHour = 0;
  cfg.nightEndHour = 6;
  cfg.nightPollMinutes = 30;
  cfg.nightTopbar = 1;
  cfg.batShowPercent = 1;
  cfg.rotation = EPD_ROTATION;
  cfg.lang = LANG_ZH;
  strcpy(cfg.portalFields, "username,password");
}

void settingsLoad() {
  EEPROM.begin(sizeof(Settings));
  EEPROM.get(0, cfg);
  bool ok = cfg.magic == CFG_MAGIC && cfg.crc == crc32((const uint8_t*)&cfg, sizeof(cfg) - 4);
  if (!ok) { LOG("settings: invalid, using defaults"); settingsDefaults(); }
  if (cfg.lang > LANG_EN) cfg.lang = LANG_ZH;   // in EEPROM written by old firmware this byte may hold portalOk's content
}

void settingsSave() {
  cfg.magic = CFG_MAGIC;
  cfg.crc = crc32((const uint8_t*)&cfg, sizeof(cfg) - 4);
  EEPROM.put(0, cfg);
  bool ok = EEPROM.commit();
  LOG("settings: saved %s", ok ? "ok" : "FAILED");
}

bool settingsUrlIsHttps() { return strncmp(cfg.serverUrl, "https://", 8) == 0; }
