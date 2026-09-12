// Homelab e-ink dashboard -- 4.2" Z96 + ESP8266
// One wakeup = run setup() to completion, then deep sleep; loop() is unused. See DESIGN.md for the design.
#include "config.h"
#include "settings.h"
#include "rtc_mem.h"
#include "clock.h"
#include "sensors.h"
#include "net.h"
#include "model.h"
#include "ui.h"
#include "keys.h"
#include "portal.h"
#include "i18n.h"
#include <ESP8266WiFi.h>
#include <user_interface.h>

static Dash dash;
static SensorData sd;
static ClockNow now;
static uint32_t tClockRead;     // millis() at the moment the time was read
static bool haveTime = false;
static bool manualBoot = false;

// ---------- Small helpers ----------
static bool inNight(const ClockNow& t) {
  if (!t.valid) return false;
  uint8_t s = cfg.nightStartHour, e = cfg.nightEndHour;
  if (s == e) return false;
  return s < e ? (t.hour >= s && t.hour < e) : (t.hour >= s || t.hour < e);
}
static uint8_t effectivePollMinutes() {
  uint8_t p = rtc.pollMinutes ? rtc.pollMinutes : cfg.pollMinutes;
  if (inNight(now)) p = cfg.nightPollMinutes;
  if (rtc.wifiFail >= 3 || rtc.portalFail >= 3) p = max<uint8_t>(p, 15);
  if (p < 1) p = 1; if (p > 60) p = 60;
  return p;
}
static void mapNetStatus(NetResult r) {
  switch (r) {
    case NET_OK: case NET_NOT_MODIFIED: rtc.netStatus = 0; rtc.netFail = 0; break;
    case NET_WIFI_FAIL: rtc.netStatus = 1; break;
    case NET_PORTAL: rtc.netStatus = 2; break;
    case NET_PORTAL_LOGIN_FAIL: rtc.netStatus = 5; break;
    case NET_TLS_CERT: rtc.netStatus = 4; break;
    default: rtc.netStatus = 3; break;
  }
  if (r != NET_OK && r != NET_NOT_MODIFIED && rtc.netFail < 250) rtc.netFail++;
}
static void ensureDash() { if (!dash.valid) modelLoadCached(dash); }
static UiStatus makeStatus(bool keysHint) {
  UiStatus st = {};
  st.netStatus = rtc.netStatus; st.lastJsonHour = rtc.lastJsonHour; st.lastJsonMinute = rtc.lastJsonMinute;
  st.haveJson = rtc.rev[0] != 0; st.rssi = rtc.lastRssi; st.freeHeap = ESP.getFreeHeap();
  st.wakeCount = rtc.wakeCount; st.fwVersion = FW_VERSION; st.ip = nullptr; st.showKeysHint = keysHint;
  return st;
}

// ---------- One network round: NTP (when due) + JSON ----------
static NetResult networkRound(bool forceNtp) {
  if (!netConnect()) { netOff(); rtc.netStatus = 1; return NET_WIFI_FAIL; }   // without this the SDK keeps auto-reconnecting, wasting radio power during the refresh
  bool ntpDue = forceNtp || !haveTime || (now.valid && now.hour == NTP_HOUR && rtc.ntpDoneDay != now.day);
  if (ntpDue) {
    ClockNow n2; if (clockNtpSync(n2)) { now = n2; haveTime = true; tClockRead = millis(); }
  }
  if (haveTime) clockToSystem(now);   // root-CA validation needs the system time
  size_t len = 0; FetchInfo fi;
  NetResult r = netFetchJson(g_jsonBuf, JSON_BUF_SIZE, len, rtc.rev, &sd, fi);
  LOG("net: %s http=%d %lums %s heap=%u stack-free=%u", netResultName(r), fi.httpCode, fi.ms, fi.err, ESP.getFreeHeap(), ESP.getFreeContStack());
  netOff();
  if (r == NET_OK) {
    static Dash nd;   // ~1.7 KB; on the stack this would blow the 4 KB cont stack (HTTPClient + the lwIP send path both live there)
    if (modelParse(g_jsonBuf, nd)) {
      bool alertRise = nd.alert && !rtc.alert;
      dash = nd;
      strncpy(rtc.rev, nd.rev, sizeof(rtc.rev) - 1);
      if (nd.nextPollSec) rtc.pollMinutes = constrain((int)(nd.nextPollSec / 60), 1, 60);
      rtc.alert = nd.alert; rtc.contentDirty = 1; rtc.lastJsonOk = 1;
      rtc.wxCode = nd.wx.valid ? nd.wx.code : 99;
      strncpy(rtc.wxText, nd.wx.valid ? nd.wx.text : "", sizeof(rtc.wxText) - 1); rtc.wxText[sizeof(rtc.wxText) - 1] = 0;
      rtc.wxLo = nd.wx.valid ? nd.wx.lo : -99; rtc.wxHi = nd.wx.valid ? nd.wx.hi : -99;
      if (alertRise) rtc.forceFull = 1;
      modelSaveCached(g_jsonBuf, len);
    } else r = NET_BAD_JSON;
  }
  if (r == NET_OK || r == NET_NOT_MODIFIED) { rtc.lastJsonOk = 1; if (now.valid) { rtc.lastJsonHour = now.hour; rtc.lastJsonMinute = now.minute; } }
  else rtc.lastJsonOk = 0;
  uint8_t before = rtc.netStatus; mapNetStatus(r);
  if (before != rtc.netStatus) rtc.contentDirty = 1;   // the status line changed, so redraw
  return r;
}

// ---------- Interactive window ----------
// After a manual reset: once the screen is refreshed, show the key hints; keys stay active for 10 s and each
// press extends the window by another 10 s
static void interactive() {
  Page page = PAGE_DASH;
  ensureDash();
  bool lp = false;
  uint8_t k = keysPending();                            // keys pressed during networking/refresh take effect immediately
  if (!k) k = keysWait(INTERACT_WINDOW_MS, lp);
  bool touched = false;
  while (k) {
    touched = true;
    LOG("key %d %s", k, lp ? "long" : "short");
    if (k == 2 && !lp)      { page = (Page)((page + 1) % PAGE_COUNT); uiDrawContentOnly(dash, makeStatus(true), page); }          // next page
    else if (k == 3 && !lp) { page = (Page)((page + PAGE_COUNT - 1) % PAGE_COUNT); uiDrawContentOnly(dash, makeStatus(true), page); } // previous page
    else if (k == 2)        { cfg.batShowPercent = !cfg.batShowPercent; settingsSave(); uiDrawTopBarOnly(now, sd); }            // long press: battery display style
    else if (k == 3)        { networkRound(false); ensureDash(); uiDrawContentOnly(dash, makeStatus(true), page); }                // long press: refresh from the network now
    k = keysPending();
    if (!k) k = keysWait(INTERACT_WINDOW_MS, lp);
  }
  LOG("interactive end (touched=%d, page=%d)", touched, page);
  uiClearStatusGhost();
  uiDrawContentOnly(dash, makeStatus(false), PAGE_DASH);   // back to the dashboard, drop the hints
  rtc.contentDirty = 0;
}

// ---------- Deep sleep ----------
static void sleepUntilNextMinute(uint8_t extraMinutes, bool nextNeedsRf) {
  uint32_t elapsedS = (millis() - tClockRead) / 1000;
  uint32_t sec = haveTime ? (now.second + elapsedS) % 60 : 0;
  int32_t ms = (int32_t)(60 - sec) * 1000 + (int32_t)extraMinutes * 60000 - 350;   // 350 ms ~ boot overhead
  if (ms < 800) ms += 60000;
  if (ms > 70L * 60000L) ms = 70L * 60000L;
  rtc.lastRunMs = millis();
  rtcSave();
  LOG("sleep %ld ms rf=%d (run %lu ms)", ms, nextNeedsRf, millis());
  Serial.flush();
  ESP.deepSleep((uint64_t)ms * 1000ULL, nextNeedsRf ? WAKE_RF_DEFAULT : WAKE_RF_DISABLED);
  while (true) delay(100);
}
static void sleepForever() { uiEnd(); netOff(); rtcSave(); ESP.deepSleep(0); while (true) delay(100); }

void setup() {
  Serial.begin(DBG_BAUD, SERIAL_8N1, SERIAL_TX_ONLY);
#ifdef WIFI_SDK_DEBUG
  Serial.setDebugOutput(true);   // build with -DWIFI_SDK_DEBUG to enable the SDK's WiFi state-machine log (association/auth status codes)
#endif
  uint32_t reason = ESP.getResetInfoPtr()->reason;
  manualBoot = reason != REASON_DEEP_SLEEP_AWAKE;
  LOG("\n== " FW_VERSION " boot reason=%u heap=%u", reason, ESP.getFreeHeap());
  WiFi.mode(WIFI_OFF); WiFi.forceSleepBegin();
  settingsLoad();
  bool rtcOk = rtcLoad();
  if (!rtcOk) { LOG("rtc mem: invalid -> reset"); rtcReset(); }
  keysInit();
#ifdef KEY_DIAG
  { LOG("KEY_DIAG: press SW2 / SW3 / side key one at a time (30 s)");
    int l0 = -1, l3 = -1; uint32_t t0 = millis();
    while (millis() - t0 < 30000) {
      int a = digitalRead(KEY2_PIN), b = digitalRead(KEY3_PIN);
      if (a != l0 || b != l3) { LOG("  t=%5lu ms  GPIO0=%d  GPIO3=%d", millis() - t0, a, b); l0 = a; l3 = b; }
      delay(20);
    }
    LOG("KEY_DIAG done"); }
#endif

  // battery too low: sleep without lighting the screen
  if (rtc.page != 0xFE || manualBoot) { float v = batteryVoltageNow(); if (v <= BAT_DEAD_V) { LOG("battery dead %.2f", v); ESP.deepSleep(0); } }

  // provisioning: hold key 3 while resetting, or when not configured yet
  bool key3 = keyDown(KEY3_PIN);
  LOG("boot: key3=%d configured=%d", key3, cfg.configured);
  if (!cfg.configured || key3) { uiBegin(true); portalRun(); }

  bool rfOn = rtc.page != 0xFE || manualBoot;   // 0xFE = this wakeup came up with RF off, so the ADC is unreliable
  sensorsRead(sd, rfOn);
  if (sd.vbat <= BAT_LOW_V && !sd.charging) {
    uiBegin(true); uiMessage(TR(S_BAT_DEAD_1), TR(S_BAT_DEAD_2));
    sleepForever();
  }

  // time: chip first, otherwise the software tick
  keysPoll();
  clockBegin();
  ClockNow chipNow;
  bool chipOk = cfg.rtcTypeKnown && clockChipRead(chipNow);
  if (!chipOk && !manualBoot) clockSoftTick();
  haveTime = clockGet(now);
  tClockRead = millis();
  LOG("time: %s %04d-%02d-%02d %02d:%02d:%02d chip=%d", haveTime ? "ok" : "none", now.year, now.month, now.day, now.hour, now.minute, now.second, rtc.rtcChipOk);


  // whether to go online
  uint8_t pollMin = effectivePollMinutes();
  bool ntpDue = !haveTime || (now.valid && now.hour == NTP_HOUR && rtc.ntpDoneDay != now.day);
  bool needNet = manualBoot || !haveTime || ntpDue || !rtc.rev[0] || (now.valid && now.minute % pollMin == 0);
  if (needNet) {
    // last wakeup had RF off but we need the network: take a short nap to come back up with RF on
    if (rtc.page == 0xFE) { rtc.page = 0; rtcSave(); ESP.deepSleep(200000, WAKE_RF_DEFAULT); }
    networkRound(false);
  }

  // night mode without top-bar refresh: only update state, never touch the display
  bool night = inNight(now);
  bool quietNight = night && !cfg.nightTopbar && !needNet && !manualBoot;

  bool full = manualBoot || rtc.forceFull || !rtcOk || (now.valid && now.minute % FULL_REFRESH_EVERY_MIN == 0 && !quietNight);
  if (!quietNight) {
    uiBegin(full);
    UiStatus st = makeStatus(manualBoot);   // manual reset: show the key hints on the very first refresh
    if (full) { ensureDash(); uiDrawAll(now, sd, dash, st, PAGE_DASH); }
    else {
      uiDrawTopBarOnly(now, sd);
      if (rtc.contentDirty) { ensureDash(); uiDrawContentOnly(dash, st, PAGE_DASH); }
    }
    rtc.contentDirty = 0; rtc.forceFull = 0;
    rtc.lastTemp10 = (int16_t)(sd.temp * 10); rtc.lastHumi10 = (int16_t)(sd.humi * 10);
    rtc.lastBatBars = sd.batBars; rtc.lastBatPct = sd.batPct;
    if (manualBoot) interactive();
    uiEnd();
  }

  rtc.wakeCount++;
  // next wakeup
  uint8_t extra = 0;
  pollMin = effectivePollMinutes();
  if (night && !cfg.nightTopbar && rtc.rtcChipOk && now.valid) {
    uint8_t toNext = pollMin - (now.minute % pollMin);           // minutes until the next poll boundary
    extra = toNext > 0 ? toNext - 1 : 0;
  }
  // Time of the next wakeup, based on "the instant the clock was read + elapsed runtime", so it stays correct
  // across a minute boundary
  uint32_t nowSec = clockSecondsOfDay(now) + (millis() - tClockRead) / 1000;
  uint32_t wakeSec = ((nowSec / 60) + 1 + extra) * 60;
  uint8_t nextMinute = (wakeSec / 60) % 60, nextHour = (wakeSec / 3600) % 24;
  bool nextNeedsRf = !haveTime || !rtc.rev[0] || rtc.netStatus != 0 || (nextMinute % pollMin == 0) ||
                     (nextMinute == 0 && nextHour == NTP_HOUR);
  rtc.page = nextNeedsRf ? 0 : 0xFE;   // 0xFE = this wakeup will come up with RF off
  sleepUntilNextMinute(extra, nextNeedsRf);
}

void loop() {}
