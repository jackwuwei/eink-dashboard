#include "model.h"
#include "config.h"
#include "i18n.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

char g_jsonBuf[JSON_BUF_SIZE];

static void cpy(char* dst, size_t n, const char* src) { if (!src) { dst[0] = 0; return; } strncpy(dst, src, n - 1); dst[n - 1] = 0; }

bool modelParse(const char* json, Dash& d) {
  memset(&d, 0, sizeof(d));
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { LOG("json: %s", e.c_str()); return false; }
  cpy(d.rev, sizeof(d.rev), doc["rev"] | "");
  d.nextPollSec = doc["next_poll"] | 0;
  d.alert = doc["alert"] | 0;
  cpy(d.ts, sizeof(d.ts), doc["ts"] | "");
  cpy(d.title, sizeof(d.title), doc["title"] | "");
  cpy(d.claudeNote, sizeof(d.claudeNote), doc["claude_note"] | "");
  for (JsonObject s : doc["servers"].as<JsonArray>()) {
    if (d.nServers >= 6) break;
    DashServer& x = d.servers[d.nServers++];
    cpy(x.name, sizeof(x.name), s["n"] | "");
    x.ok = (s["ok"] | 0) != 0;
    x.cpu = s["cpu"].isNull() ? -1 : (int16_t)(s["cpu"] | 0);
    x.mem = s["mem"].isNull() ? -1 : (int16_t)(s["mem"] | 0);
    x.temp = s["temp"].isNull() ? -999 : (int16_t)(s["temp"] | 0);
    { // Chinese uptime "20天22小时" / "3小时" -> "20.9天" / "0.1天"
      const char* up = s["up"] | ""; float days = -1;
      int dd = 0, hh = 0; const char* pd = strstr(up, "天"); const char* ph = strstr(up, "小时");
      if (pd) { dd = atoi(up); if (ph) hh = atoi(pd + 3); days = dd + hh / 24.0f; }
      else if (ph) { hh = atoi(up); days = hh / 24.0f; }
      if (days >= 0) snprintf(x.up, sizeof(x.up), TR(S_DAYS_SUFFIX), days); else cpy(x.up, sizeof(x.up), up); }
    cpy(x.detail, sizeof(x.detail), s["d"] | "");
    for (JsonArray kv : s["x"].as<JsonArray>()) {
      if (x.nExtra >= 3 || kv.size() < 2) break;
      cpy(x.extraK[x.nExtra], 16, kv[0] | ""); cpy(x.extraV[x.nExtra], 16, kv[1] | ""); x.nExtra++;
    }
  }
  for (JsonObject c : doc["claude"].as<JsonArray>()) {
    if (d.nLimits >= 4) break;
    DashLimit& x = d.limits[d.nLimits++];
    cpy(x.label, sizeof(x.label), c["l"] | "");
    x.pct = (uint8_t)constrain((int)(c["pct"] | 0), 0, 100);
    cpy(x.reset, sizeof(x.reset), c["r"] | "");
  }
  for (JsonObject e2 : doc["env"].as<JsonArray>()) {
    if (d.nEnv >= 4) break;
    DashEnv& x = d.env[d.nEnv++];
    cpy(x.icon, sizeof(x.icon), e2["i"] | ""); cpy(x.label, sizeof(x.label), e2["l"] | ""); cpy(x.value, sizeof(x.value), e2["v"] | "");
  }
  JsonObject w = doc["w"];
  if (!w.isNull()) {
    Weather& x = d.wx; x.valid = true;
    cpy(x.city, sizeof(x.city), w["city"] | ""); x.temp = w["t"] | 0; x.code = w["c"] | 99; cpy(x.text, sizeof(x.text), w["txt"] | "");
    x.lo = w["lo"].isNull() ? -99 : (int8_t)(w["lo"] | 0); x.hi = w["hi"].isNull() ? -99 : (int8_t)(w["hi"] | 0);
    x.humi = w["h"].isNull() ? -99 : (int8_t)(w["h"] | 0);
    cpy(x.windDir, sizeof(x.windDir), w["wd"] | ""); cpy(x.windScale, sizeof(x.windScale), w["ws"] | "");
    cpy(x.sunrise, sizeof(x.sunrise), w["sr"] | ""); cpy(x.sunset, sizeof(x.sunset), w["ss"] | "");
    cpy(x.tip, sizeof(x.tip), w["tip"] | ""); cpy(x.updated, sizeof(x.updated), w["up"] | "");
    for (JsonObject dd : w["d"].as<JsonArray>()) {
      if (x.nDays >= 4) break;
      WxDay& y = x.days[x.nDays++];
      cpy(y.date, sizeof(y.date), dd["d"] | ""); cpy(y.wk, sizeof(y.wk), dd["w"] | ""); y.code = dd["c"] | 99;
      cpy(y.text, sizeof(y.text), dd["t"] | ""); y.lo = dd["lo"] | 0; y.hi = dd["hi"] | 0;
    }
  }
  d.valid = true;
  return true;
}

bool modelLoadCached(Dash& d) {
  if (!LittleFS.begin()) { LOG("fs: mount failed"); return false; }
  File f = LittleFS.open("/last.json", "r");
  if (!f) { LittleFS.end(); return false; }
  size_t n = f.readBytes(g_jsonBuf, JSON_BUF_SIZE - 1); g_jsonBuf[n] = 0; f.close(); LittleFS.end();
  return n > 0 && modelParse(g_jsonBuf, d);
}

void modelSaveCached(const char* json, size_t len) {
  if (!LittleFS.begin()) { LOG("fs: mount failed"); return; }
  File f = LittleFS.open("/last.json", "w");
  if (f) { f.write((const uint8_t*)json, len); f.close(); }
  LittleFS.end();
}
