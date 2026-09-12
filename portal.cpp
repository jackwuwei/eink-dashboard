#include "portal.h"
#include "portal_html.h"
#include "config.h"
#include "settings.h"
#include "rtc_mem.h"
#include "ui.h"
#include "net.h"
#include "sensors.h"
#include "clock.h"
#include "i18n.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

static ESP8266WebServer server(80);
static DNSServer dns;
static uint32_t lastActivity;
static enum { STA_IDLE, STA_CONNECTING, STA_CONNECTED, STA_FAILED } staState = STA_IDLE;
static uint32_t staStart, restartAt = 0;
static bool staRetried = false;   // the post-save connection has already switched PHY once
static SensorData psd;

static String apIp() { return AP_IP.toString(); }
static void redirectHome() { server.sendHeader("Location", "http://" + apIp() + "/", true); server.send(302, "text/plain", ""); }
static bool hostIsUs() { String h = server.hostHeader(); return h == apIp() || h.startsWith(apIp() + ":"); }
static void touch() { lastActivity = millis(); }

static void handleRoot() {
  touch();
  if (!hostIsUs()) { redirectHome(); return; }
  server.send_P(200, "text/html; charset=utf-8", PORTAL_HTML);
}

static void handleScan() {
  touch();
  int n = WiFi.scanNetworks(false, false);
  JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["s"] = WiFi.SSID(i); o["r"] = WiFi.RSSI(i); o["e"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out; serializeJson(doc, out); server.send(200, "application/json", out);
}

static String fpHex() {
  String s; char b[4];
  for (int i = 0; i < 20; i++) { snprintf(b, sizeof(b), "%02X", cfg.fingerprint[i]); s += b; if (i < 19) s += ':'; }
  return s;
}
static bool fpParse(const char* txt, uint8_t out[20]) {
  String s(txt); s.replace(":", ""); s.replace(" ", ""); if (s.length() != 40) return false;
  for (int i = 0; i < 20; i++) out[i] = strtoul(s.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return true;
}

static void handleConfig() {
  touch();
  JsonDocument d;
  d["ssid"] = cfg.ssid; d["url"] = cfg.serverUrl; d["tlsMode"] = cfg.tlsMode; d["fp"] = fpHex();
  d["poll"] = cfg.pollMinutes; d["npoll"] = cfg.nightPollMinutes; d["nstart"] = cfg.nightStartHour; d["nend"] = cfg.nightEndHour;
  d["ntop"] = cfg.nightTopbar; d["bat"] = cfg.batShowPercent; d["rot"] = cfg.rotation;
  d["lang"] = cfg.lang;
  d["pen"] = cfg.portalEnable; d["purl"] = cfg.portalUrl; d["puser"] = cfg.portalUser; d["pfields"] = cfg.portalFields; d["pextra"] = cfg.portalExtra; d["pok"] = cfg.portalOk;
  d["configured"] = cfg.configured; d["fw"] = FW_VERSION;
  d["vbat"] = serialized(String(psd.vbat, 2)); d["t"] = serialized(String(psd.temp, 1)); d["h"] = serialized(String(psd.humi, 0)); d["rtc"] = cfg.rtcTypeKnown;
  String out; serializeJson(d, out); server.send(200, "application/json", out);
}

static void cpyS(char* dst, size_t n, JsonVariant v, const char* keep) { const char* s = v | keep; strncpy(dst, s ? s : "", n - 1); dst[n - 1] = 0; }

static void applyForm(JsonDocument& f, bool withSecrets) {
  cpyS(cfg.ssid, sizeof(cfg.ssid), f["ssid"], cfg.ssid);
  if (withSecrets) { const char* p = f["pass"] | ""; if (p[0] || strcmp(cfg.ssid, f["ssid"] | "") != 0) strncpy(cfg.pass, p, sizeof(cfg.pass) - 1); }
  cpyS(cfg.serverUrl, sizeof(cfg.serverUrl), f["url"], cfg.serverUrl);
  cfg.tlsMode = f["tlsMode"] | 0; if (cfg.tlsMode > 2) cfg.tlsMode = 0;
  if (f["fp"].is<const char*>()) fpParse(f["fp"], cfg.fingerprint);
  cfg.pollMinutes = constrain((int)(f["poll"] | 5), 1, 60);
  cfg.nightPollMinutes = constrain((int)(f["npoll"] | 30), 1, 60);
  cfg.nightStartHour = constrain((int)(f["nstart"] | 0), 0, 23);
  cfg.nightEndHour = constrain((int)(f["nend"] | 6), 0, 23);
  cfg.nightTopbar = (f["ntop"] | 1) ? 1 : 0;
  cfg.batShowPercent = (f["bat"] | 1) ? 1 : 0;
  cfg.rotation = (f["rot"] | 0) == 2 ? 2 : 0;
  cfg.lang = (f["lang"] | 0) == LANG_EN ? LANG_EN : LANG_ZH;
  cfg.portalEnable = (f["pen"] | 0) ? 1 : 0;
  cpyS(cfg.portalUrl, sizeof(cfg.portalUrl), f["purl"], cfg.portalUrl);
  cpyS(cfg.portalUser, sizeof(cfg.portalUser), f["puser"], cfg.portalUser);
  if (withSecrets) { const char* p = f["ppass"] | ""; if (p[0]) strncpy(cfg.portalPass, p, sizeof(cfg.portalPass) - 1); }
  cpyS(cfg.portalFields, sizeof(cfg.portalFields), f["pfields"], cfg.portalFields);
  cpyS(cfg.portalExtra, sizeof(cfg.portalExtra), f["pextra"], cfg.portalExtra);
  cpyS(cfg.portalOk, sizeof(cfg.portalOk), f["pok"], cfg.portalOk);
}

static bool staWait(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) { if (WiFi.status() == WL_CONNECTED) return true; dns.processNextRequest(); server.handleClient(); delay(50); }
  return false;
}
static bool staConnectBlocking(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) return true;
  WiFi.disconnect(); delay(50);
  WiFi.begin(ssid, pass);
  if (staWait(timeoutMs)) return true;
  // retry with the other PHY mode (same fallback as netConnect)
  uint8_t other = rtc.phyMode ? 0 : 1;
  LOG("portal: sta retry with 11%c (reason %d)", other ? 'n' : 'g', netLastDisconnectReason());
  WiFi.disconnect(); delay(50); netApplyPhy(other); WiFi.begin(ssid, pass);
  if (staWait(timeoutMs)) { rtc.phyMode = other; return true; }
  netApplyPhy(rtc.phyMode);
  return false;
}

static void handleTest() {
  touch();
  JsonDocument f; if (deserializeJson(f, server.arg("plain"))) { server.send(400, "application/json", "{\"res\":\"bad-request\"}"); return; }
  Settings backup = cfg;
  applyForm(f, true);
  if (cfg.pass[0] && netSsidIsOpen(cfg.ssid)) { LOG("portal: '%s' is open, ignoring password", cfg.ssid); cfg.pass[0] = 0; }
  JsonDocument r;
  uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), TR(S_PORTAL_TESTING));
  if (!staConnectBlocking(cfg.ssid, cfg.pass, 20000)) {
    r["res"] = "wifi-fail"; r["err"] = TR(S_PORTAL_WIFI_FAIL);
  } else {
    r["ip"] = WiFi.localIP().toString();
    FetchInfo fi;
    NetResult res = netTestFetch(cfg.serverUrl, cfg.tlsMode, cfg.fingerprint, g_jsonBuf, JSON_BUF_SIZE, fi);
    r["res"] = netResultName(res); r["code"] = fi.httpCode; r["ms"] = fi.ms; r["err"] = fi.err;
    if (res == NET_OK) { JsonDocument j; if (!deserializeJson(j, g_jsonBuf)) r["rev"] = j["rev"] | ""; }
  }
  cfg = backup;   // testing must not change the settings
  String out; serializeJson(r, out); server.send(200, "application/json", out);
  { char b[80]; snprintf(b, sizeof(b), TR(S_PORTAL_TEST_RES), (const char*)(r["res"] | "")); uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), b); }
}

static void handleSave() {
  touch();
  JsonDocument f; if (deserializeJson(f, server.arg("plain"))) { server.send(400, "application/json", "{\"ok\":0}"); return; }
  applyForm(f, true);
  if (cfg.pass[0] && netSsidIsOpen(cfg.ssid)) { LOG("portal: '%s' is open, dropping password", cfg.ssid); cfg.pass[0] = 0; }
  cfg.configured = 1;
  settingsSave();
  rtc.wifiCacheOk = 0; rtc.tlsSessionOk = 0; rtc.contentDirty = 1; rtc.forceFull = 1; rtc.rev[0] = 0; rtcSave();
  server.send(200, "application/json", "{\"ok\":1}");
  staState = STA_CONNECTING; staStart = millis(); staRetried = false;
  WiFi.disconnect(); delay(50); WiFi.begin(cfg.ssid, cfg.pass);
  { char b[80]; snprintf(b, sizeof(b), TR(S_PORTAL_CONNECTING), cfg.ssid); uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), b); }
}

static void handleStatus() {
  touch();
  JsonDocument d;
  d["sta"] = staState == STA_CONNECTED ? "connected" : staState == STA_FAILED ? "failed" : staState == STA_CONNECTING ? "connecting" : "idle";
  d["ip"] = WiFi.localIP().toString(); d["vbat"] = serialized(String(psd.vbat, 2)); d["fw"] = FW_VERSION;
  String out; serializeJson(d, out); server.send(200, "application/json", out);
}

static void handleReset() {
  touch();
  settingsDefaults(); settingsSave(); rtcReset();
  server.send(200, "application/json", "{\"ok\":1}");
  restartAt = millis() + 1500;
}

void portalRun() {
  LOG("portal: start");
  sensorsRead(psd, true);
  clockBegin();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  if (rtc.phyMode > 1) rtc.phyMode = 0;
  netApplyPhy(rtc.phyMode);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS, random(1, 12), 0, 2);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  LOG("portal: AP %s up, ip %s", AP_SSID, apIp().c_str());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/reset", HTTP_POST, handleReset);
  // connectivity-check paths of the various OSes -> 302 to the config page (captive-portal popup)
  const char* probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html", "/ncsi.txt",
                          "/connecttest.txt", "/redirect", "/fwlink", "/canonical.html", "/success.txt", "/chat", "/mobile/status.php"};
  for (auto p : probes) server.on(p, [] { touch(); redirectHome(); });
  server.onNotFound([] { touch(); redirectHome(); });
  server.begin();
  LOG("portal: http up, heap=%u", ESP.getFreeHeap());
  uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), cfg.configured ? TR(S_PORTAL_AGAIN) : TR(S_PORTAL_FIRST));
  touch();
  while (true) {
    dns.processNextRequest();
    server.handleClient();
    if (staState == STA_CONNECTING) {
      if (WiFi.status() == WL_CONNECTED) {
        staState = STA_CONNECTED;
        if (staRetried) { rtc.phyMode = rtc.phyMode ? 0 : 1; rtcSave(); }   // only connected after switching PHY, so remember it
        String ip = WiFi.localIP().toString();
        LOG("portal: sta connected %s (11%c)", ip.c_str(), rtc.phyMode ? 'n' : 'g');
        { char b[80]; snprintf(b, sizeof(b), TR(S_PORTAL_CONNECTED), ip.c_str()); uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), b); }
        restartAt = millis() + 5000;
      } else if (millis() - staStart > 20000 && !staRetried) {
        staRetried = true; staStart = millis();
        uint8_t other = rtc.phyMode ? 0 : 1;
        LOG("portal: sta retry with 11%c (reason %d)", other ? 'n' : 'g', netLastDisconnectReason());
        WiFi.disconnect(); delay(50); netApplyPhy(other); WiFi.begin(cfg.ssid, cfg.pass);
        { char b[80]; snprintf(b, sizeof(b), TR(S_PORTAL_RETRY_PHY), other ? "n" : "g", cfg.ssid); uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), b); }
      } else if (millis() - staStart > 20000) {
        netApplyPhy(rtc.phyMode);
        staState = STA_FAILED;
        LOG("portal: sta connect failed status=%d ssid='%s' pwlen=%d", WiFi.status(), cfg.ssid, strlen(cfg.pass));
        { char b[80]; snprintf(b, sizeof(b), TR(S_PORTAL_FAILED), cfg.ssid); uiPortalScreen(AP_SSID, AP_PASS, apIp().c_str(), b); }
      }
    }
    if (restartAt && millis() > restartAt) { delay(200); ESP.restart(); }
    if (millis() - lastActivity > PORTAL_TIMEOUT_MS) {
      LOG("portal: timeout -> sleep");
      uiMessage(TR(S_PORTAL_TIMEOUT_1), TR(S_PORTAL_TIMEOUT_2));
      uiEnd(); WiFi.mode(WIFI_OFF); ESP.deepSleep(0);
    }
    delay(2);
  }
}
