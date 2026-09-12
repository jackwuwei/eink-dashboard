#include "ui.h"
#include "config.h"
#include "settings.h"
#include "rtc_mem.h"
#include <GxEPD2_BW.h>
#include "GxEPD2_420_Z96.h"
#include <U8g2_for_Adafruit_GFX.h>
#include <math.h>
#include "fonts_noto.h"
#include "keys.h"
#include "lunar.h"
#include "i18n.h"
#include "weather_icons.h"
#include <user_interface.h>

static GxEPD2_BW<GxEPD2_420_Z96, GxEPD2_420_Z96::HEIGHT / 4> display(GxEPD2_420_Z96(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX g_u8;
#define BLACK GxEPD_BLACK
#define WHITE GxEPD_WHITE
#define W 400
#define H 300
#define TOP_H 50

#define F_TIME  u8g2_font_logisoso38_tn
#define F_NAME  u8g2_font_noto18_hd
#define F_TXT   u8g2_font_noto16_hd
#define F_SM    u8g2_font_noto14_hd

static void font(const uint8_t* f) { g_u8.setFont(f); }
static void txt(int x, int y, const char* s) { g_u8.setCursor(x, y); g_u8.print(s); }
static int tw(const char* s) { return g_u8.getUTF8Width(s); }
static void txtRight(int xr, int y, const char* s) { txt(xr - tw(s), y, s); }
static void txtCenter(int xc, int y, const char* s) { txt(xc - tw(s) / 2, y, s); }
static const SensorData* g_sd = nullptr;
static ClockNow g_now = {};
static void fg(uint16_t c) { g_u8.setForegroundColor(c); g_u8.setBackgroundColor(c == BLACK ? WHITE : BLACK); }

static bool g_fullPending = false;
// Same as the stock firmware: always init(0,0,10,0) + partial refresh; a "full refresh" means painting the
// whole screen black then white (BW_refresh) to clear ghosting
void uiBegin(bool fullRefreshMode) {
  g_fullPending = fullRefreshMode;
  system_update_cpu_freq(160);              // speed up while drawing (u8g2 fetching glyphs from flash is slow)
  display.init(0, false, 10, false);
  display.epd2.setBusyCallback([](const void*) { keysPoll(); });   // poll keys during the refresh busy-wait too
  LOG("ui: init full=%d", fullRefreshMode);
  display.setRotation(cfg.rotation);
  g_u8.begin(display);
  g_u8.setFontMode(1);
  g_u8.setFontDirection(0);
  fg(BLACK);
}
void uiEnd() { display.powerOff(); system_update_cpu_freq(80); }

// ---------- Icons (drawn vectors, to avoid bitmap assets) ----------
static void iconTemp(int x, int y) {      // thermometer 10x16
  display.drawRoundRect(x + 3, y, 4, 11, 2, BLACK);
  display.fillCircle(x + 5, y + 13, 3, BLACK);
  display.drawFastVLine(x + 5, y + 4, 8, BLACK);
}
static void iconDrop(int x, int y) {      // water drop 10x16
  display.fillTriangle(x + 5, y, x + 1, y + 9, x + 9, y + 9, BLACK);
  display.fillCircle(x + 5, y + 10, 4, BLACK);
  display.fillCircle(x + 4, y + 10, 1, WHITE);
}
static void iconFan(int x, int y) {
  display.drawCircle(x + 6, y + 7, 6, BLACK);
  display.fillCircle(x + 6, y + 7, 2, BLACK);
  display.drawLine(x + 6, y + 1, x + 6, y + 13, BLACK); display.drawLine(x, y + 7, x + 12, y + 7, BLACK);
}
static void iconBattery(int x, int y, uint8_t bars, bool charging) {  // 24x12
  display.drawRect(x, y, 21, 12, BLACK); display.fillRect(x + 21, y + 3, 2, 6, BLACK);
  for (uint8_t i = 0; i < bars && i < 3; i++) display.fillRect(x + 2 + i * 6, y + 2, 5, 8, BLACK);
  if (charging) {  // lightning bolt
    display.fillTriangle(x + 12, y - 3, x + 7, y + 7, x + 11, y + 7, WHITE);
    display.fillTriangle(x + 9, y + 15, x + 14, y + 5, x + 10, y + 5, WHITE);
    display.drawLine(x + 12, y - 3, x + 7, y + 7, BLACK); display.drawLine(x + 7, y + 7, x + 11, y + 7, BLACK);
    display.drawLine(x + 11, y + 7, x + 9, y + 15, BLACK); display.drawLine(x + 9, y + 15, x + 14, y + 5, BLACK);
    display.drawLine(x + 14, y + 5, x + 10, y + 5, BLACK); display.drawLine(x + 10, y + 5, x + 12, y - 3, BLACK);
  }
}
// WiFi signal fan icon 22x12: 3 arcs + a dot, bars = number of lit arcs (inner to outer)
static void iconWifi(int x, int y, uint8_t bars) {
  int cx = x + 11, cy = y + 12;             // the fan's center sits at the bottom
  const uint8_t r[3] = {4, 8, 12};
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t c = i < bars ? BLACK : WHITE;
    if (c == WHITE) continue;
    display.drawCircleHelper(cx, cy, r[i], 0x3, BLACK);
    display.drawCircleHelper(cx, cy, r[i] - 1, 0x3, BLACK);
  }
  // clip everything outside the 90-degree fan (one triangle on each side)
  display.fillTriangle(cx - 14, cy + 1, cx - 14, cy - 14, cx, cy + 1, WHITE);
  display.fillTriangle(cx + 14, cy + 1, cx + 14, cy - 14, cx, cy + 1, WHITE);
  display.fillCircle(cx, cy - 1, 1, BLACK);
}
// Small top-bar weather icons 22x18, grouped by Seniverse weather code: clear / partly cloudy / overcast /
// rain / thunder / snow / haze / wind
static void miniCloud(int x, int y) {         // cloud 18x10, top-left corner at (x,y)
  display.fillCircle(x + 6, y + 6, 4, BLACK); display.fillCircle(x + 11, y + 4, 5, BLACK); display.fillCircle(x + 14, y + 7, 3, BLACK);
  display.fillRect(x + 4, y + 7, 12, 4, BLACK);
  display.fillCircle(x + 6, y + 6, 2, WHITE); display.fillCircle(x + 11, y + 4, 3, WHITE); display.fillCircle(x + 14, y + 7, 1, WHITE);
  display.fillRect(x + 5, y + 7, 10, 2, WHITE); display.drawFastHLine(x + 4, y + 10, 12, BLACK);
}
static void miniSun(int cx, int cy, int r) {
  display.drawCircle(cx, cy, r, BLACK);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7854f; int x0 = cx + (r + 2) * cosf(a), y0 = cy + (r + 2) * sinf(a), x1 = cx + (r + 4) * cosf(a), y1 = cy + (r + 4) * sinf(a);
    display.drawLine(x0, y0, x1, y1, BLACK);
  }
}
static void iconWxMini(int x, int y, uint8_t c) {
  if (c <= 3 || c == 37 || c == 38) { miniSun(x + 11, y + 9, 4); return; }
  if (c >= 26 && c <= 31) { for (int i = 0; i < 3; i++) display.drawFastHLine(x + 2 + (i % 2) * 3, y + 4 + i * 5, 14, BLACK); return; }
  if (c >= 32 && c <= 36) { display.drawFastHLine(x + 2, y + 5, 12, BLACK); display.drawFastHLine(x + 2, y + 9, 17, BLACK); display.drawFastHLine(x + 2, y + 13, 9, BLACK); return; }
  if (c == 4) { miniSun(x + 15, y + 5, 3); miniCloud(x, y + 6); return; }
  miniCloud(x, y + 1);
  if (c >= 10 && c <= 19 && c != 11 && c != 12) { for (int i = 0; i < 3; i++) display.drawLine(x + 6 + i * 4, y + 13, x + 5 + i * 4, y + 17, BLACK); }
  else if (c == 11 || c == 12) { display.drawLine(x + 11, y + 11, x + 8, y + 15, BLACK); display.drawFastHLine(x + 8, y + 15, 4, BLACK); display.drawLine(x + 12, y + 15, x + 9, y + 19, BLACK); }
  else if (c >= 20 && c <= 25) { for (int i = 0; i < 3; i++) display.fillRect(x + 5 + i * 4, y + 14 + (i % 2) * 2, 2, 2, BLACK); }
}
static void dot(int x, int y, bool filled) { if (filled) display.fillCircle(x, y, 4, BLACK); else display.drawCircle(x, y, 4, BLACK); }
static void meter(int x, int y, int w, int h, uint8_t pct) {
  display.drawRect(x, y, w, h, BLACK);
  int fw = (w - 2) * pct / 100; if (fw > 0) display.fillRect(x + 1, y + 1, fw, h - 2, BLACK);
}

// ---------- Table-header icons ----------
static void iconCpu(int x, int y) {      // CPU chip 14x14
  display.drawRect(x + 3, y + 3, 8, 8, BLACK); display.fillRect(x + 5, y + 5, 4, 4, BLACK);
  const int8_t k[] = {4, 7, 9};
  for (int8_t p : k) { display.drawFastVLine(x + p, y, 3, BLACK); display.drawFastVLine(x + p, y + 11, 3, BLACK); display.drawFastHLine(x, y + p, 3, BLACK); display.drawFastHLine(x + 11, y + p, 3, BLACK); }
}
static void iconRam(int x, int y) {      // RAM stick 18x12
  display.drawRect(x, y + 1, 18, 9, BLACK);
  for (int k = 0; k < 4; k++) display.fillRect(x + 2 + k * 4, y + 3, 3, 4, BLACK);
  for (int k = 0; k < 6; k++) display.drawFastVLine(x + 2 + k * 3, y + 10, 3, BLACK);
}
static void iconClock(int x, int y) {    // clock 14x14
  display.drawCircle(x + 7, y + 7, 7, BLACK); display.drawFastVLine(x + 7, y + 3, 5, BLACK); display.drawLine(x + 7, y + 7, x + 10, y + 9, BLACK);
}
static void iconClaude(int cx, int cy, int s) {   // 12-ray starburst (same as the Kindle dashboard)
  for (int i = 0; i < 12; i++) {
    float a = (i * 30 + 15) * 3.14159f / 180.0f;
    float ro = s * (i % 2 == 0 ? 0.48f : 0.36f), ri = s * 0.10f;
    int x0 = cx + ri * cosf(a), y0 = cy + ri * sinf(a), x1 = cx + ro * cosf(a), y1 = cy + ro * sinf(a);
    display.drawLine(x0, y0, x1, y1, BLACK); display.drawLine(x0 + 1, y0, x1 + 1, y1, BLACK);
  }
}

// ---------- Top bar (h=50) ----------
static void drawTopBar(const ClockNow& t, const SensorData& sd) {
  display.fillRect(0, 0, W, TOP_H, WHITE);
  char b[32];
  fg(BLACK);
  if (t.valid) {
    font(F_TIME); snprintf(b, sizeof(b), "%02d:%02d", t.hour, t.minute); txt(4, 42, b);
    font(F_TXT); txt(132, 22, clockWeekdayName(t.weekday));
    font(F_SM); snprintf(b, sizeof(b), "%02d-%02d%s", t.month, t.day, rtc.rtcChipOk ? "" : "~"); txt(132, 42, b);
  } else { font(F_TXT); txt(6, 36, "--:--"); }
  // Two lines in the middle: the forecast on top (icon + text + low/high), local temperature/humidity below
  int x = 184;
  if (rtc.wxCode != 99 && rtc.rev[0]) {
    iconWxMini(x, 6, rtc.wxCode);
    font(F_SM); String s = i18nWeatherText(rtc.wxCode, rtc.wxText);
    if (rtc.wxLo > -99 && rtc.wxHi > -99) { snprintf(b, sizeof(b), " %d~%d°C", rtc.wxLo, rtc.wxHi); s += b; }
    txt(x + 26, 19, s.c_str());
  }
  if (sd.shtOk) {
    font(F_TXT);
    iconTemp(x, 29); snprintf(b, sizeof(b), "%.1f°C", sd.temp); txt(x + 14, 44, b); x += 14 + tw(b) + 10;
    iconDrop(x, 30); snprintf(b, sizeof(b), "%.0f%%", sd.humi); txt(x + 14, 44, b);
  } else if (rtc.shtFail < 3) { font(F_SM); txt(x, 44, TR(S_TH_UNKNOWN)); }
  // Right side: battery text at the far right (percentage / voltage, toggled by long-pressing SW2), then the
// battery and WiFi icons to its left
  font(F_SM);
  if (cfg.batShowPercent) snprintf(b, sizeof(b), "%d%%", sd.batPct); else snprintf(b, sizeof(b), "%.2fV", sd.vbat);
  txtRight(W - 4, 35, b);
  int bx = W - 4 - tw(b) - 6 - 24;
  iconBattery(bx, 24, sd.batBars, sd.charging);
  if (rtc.lastRssi) iconWifi(bx - 6 - 22, 22, rtc.lastRssi >= -60 ? 3 : rtc.lastRssi >= -72 ? 2 : 1);
  display.drawFastHLine(0, TOP_H, W, BLACK);
}

// ---------- Status line ----------
static const char* netStatusText(uint8_t s) {
  switch (s) { case 1: return TR(S_NET_WIFI); case 2: return TR(S_NET_PORTAL); case 3: return TR(S_NET_SRV_OFF); case 4: return TR(S_NET_CERT); case 5: return TR(S_NET_PORTAL_LOGIN); default: return nullptr; }
}
static void drawStatusLine(const Dash& d, const UiStatus& st, int y, const char* left = nullptr) {
  font(F_SM); char b[64];
  String s;
  if (left) { txt(8, y, left); if (st.showKeysHint) txtRight(W - 8, y, TR(S_KEYS_HINT)); return; }
  if (st.haveJson) { snprintf(b, sizeof(b), TR(S_UPDATED_AT), st.lastJsonHour, st.lastJsonMinute); s = b; }
  else s = TR(S_NO_DATA);
  if (d.valid && d.alert) { snprintf(b, sizeof(b), " · ⚠ %d", d.alert); s += b; }
  const char* ns = netStatusText(st.netStatus);
  if (ns) { s += " · "; s += ns; }
  if (st.showKeysHint) { s += "  "; s += TR(S_KEYS_HINT); }
  txt(8, y, s.c_str());
}

// ---------- Dashboard page ----------
static void drawDashboard(const Dash& d, const UiStatus& st) {
  fg(BLACK);
  if (!d.valid) {
    font(F_TXT); txtCenter(W / 2, 150, TR(S_WAIT_SERVER));
    font(F_SM); txtCenter(W / 2, 175, cfg.serverUrl);
    drawStatusLine(d, st, H - 7); return;
  }
  // header icons (drawn once)
  const int hy = TOP_H + 7;
  iconCpu(130, hy); iconRam(224, hy + 1); iconTemp(315, hy - 1); iconClock(W - 37, hy);   // centered on the uptime column
  // server rows
  const int ROW = 32; int y = TOP_H + 20;
  uint8_t n = d.nServers > 4 ? 4 : d.nServers;
  for (uint8_t i = 0; i < n; i++) {
    const DashServer& s = d.servers[i];
    int ry = y + i * ROW; char b[48];
    if (!s.ok) {
      display.fillRect(0, ry + 2, W, ROW - 4, BLACK);
      fg(WHITE); font(F_NAME); txt(8, ry + 23, s.name);
      font(F_SM); snprintf(b, sizeof(b), TR(S_OFFLINE_DETAIL), s.detail); txtRight(W - 8, ry + 22, b);
      fg(BLACK); continue;
    }
    font(F_NAME); txt(8, ry + 23, s.name);
    font(F_SM);
    if (s.cpu >= 0) { meter(113, ry + 9, 48, 12, s.cpu); snprintf(b, sizeof(b), "%d%%", s.cpu); txt(165, ry + 22, b); }
    if (s.mem >= 0) { meter(209, ry + 9, 48, 12, s.mem); snprintf(b, sizeof(b), "%d%%", s.mem); txt(261, ry + 22, b); }
    if (s.temp > -999) { snprintf(b, sizeof(b), "%d°C", s.temp); txt(305, ry + 22, b); }
    if (s.up[0]) txtRight(W - 8, ry + 22, s.up);
  }
  y += 4 * ROW;
  display.drawFastHLine(0, y, W, BLACK); y += 5;
  // Claude
  if (d.nLimits || d.claudeNote[0]) {
    font(F_SM);
    iconClaude(16, y + 12, 16); txt(30, y + 18, "Claude");
    if (!d.nLimits) { txt(86, y + 18, d.claudeNote); y += 24; }
    uint8_t m = d.nLimits > 3 ? 3 : d.nLimits;
    for (uint8_t i = 0; i < m; i++) {
      const DashLimit& l = d.limits[i]; char b[40];
      int yy = y + i * 24;
      txt(86, yy + 18, l.label);
      meter(174, yy + 7, 76, 12, l.pct);
      snprintf(b, sizeof(b), "%d%%", l.pct); txt(256, yy + 18, b);
      snprintf(b, sizeof(b), TR(S_RESET_AT), l.reset); txtRight(W - 8, yy + 18, b);
    }
    y += m * 24 + 1;
    display.drawFastHLine(0, y, W, BLACK);
  }
  drawStatusLine(d, st, H - 7);
}

// ---------- Detail pages (coordinates in preview/mock_pages.py) ----------
static void pageTitle(const char* t) { fg(BLACK); font(F_TXT); txt(8, TOP_H + 24, t); display.drawFastHLine(0, TOP_H + 30, W, BLACK); }

static void drawServersPage(const Dash& d, const UiStatus& st) {
  pageTitle(TR(S_T_SERVERS));
  int y = TOP_H + 36; char b[96];
  for (uint8_t i = 0; i < d.nServers && y + 40 < H - 30; i++) {
    const DashServer& s = d.servers[i];
    font(F_TXT); txt(8, y + 16, s.name);
    font(F_SM); snprintf(b, sizeof(b), "%s %s", s.ok ? TR(S_ONLINE) : TR(S_OFFLINE), s.detail); txtRight(W - 8, y + 16, b);
    if (s.ok) {
      if (s.cpu >= 0) { txt(20, y + 34, "CPU"); meter(50, y + 23, 44, 12, s.cpu); snprintf(b, sizeof(b), "%d%%", s.cpu); txt(98, y + 34, b); }
      if (s.mem >= 0) { txt(136, y + 34, TR(S_MEM)); meter(168, y + 23, 44, 12, s.mem); snprintf(b, sizeof(b), "%d%%", s.mem); txt(216, y + 34, b); }
      if (s.temp > -999) { snprintf(b, sizeof(b), "%d°C", s.temp); txt(256, y + 34, b); }
      txtRight(W - 8, y + 34, s.up);   // extras (such as the online-device count) are omitted to avoid crowding
    } else txt(20, y + 34, "—");
    y += 42;
  }
  if (d.nEnv) {
    display.drawFastHLine(0, y - 4, W, BLACK);
    String l; font(F_SM);
    for (uint8_t i = 0; i < d.nEnv; i++) { const DashEnv& e = d.env[i]; if (e.label[0]) { l += e.label; l += " "; } l += e.value; l += "   "; }
    txt(8, y + 14, l.c_str());
  }
  drawStatusLine(d, st, H - 7);
}
static void drawClaudePage(const Dash& d, const UiStatus& st) {
  pageTitle(TR(S_T_CLAUDE));
  int y = TOP_H + 44; char b[40]; 
  if (d.claudeNote[0]) { font(F_SM); txt(8, y + 16, d.claudeNote); y += 24; }
  for (uint8_t i = 0; i < d.nLimits && y + 40 < H - 20; i++) {
    const DashLimit& l = d.limits[i];
    font(F_TXT); txt(8, y + 16, l.label);
    font(F_SM); snprintf(b, sizeof(b), TR(S_RESET_AT), l.reset); txtRight(W - 8, y + 16, b);
    meter(8, y + 24, 330, 16, l.pct);
    font(F_TXT); snprintf(b, sizeof(b), "%d%%", l.pct); txtRight(W - 8, y + 38, b);
    y += 56;
  }
  drawStatusLine(d, st, H - 7);
}
static void drawDevicePage(const Dash& d, const UiStatus& st, const SensorData& sd) {
  pageTitle(TR(S_T_DEVICE));
  int y = TOP_H + 40; char b[96]; font(F_SM);
  auto row = [&](const char* k, const char* v) { txt(8, y + 14, k); txt(76, y + 14, v); y += 22; };
  snprintf(b, sizeof(b), TR(S_FW_VAL), st.fwVersion, st.wakeCount, st.freeHeap); row(TR(S_K_FW), b);
  snprintf(b, sizeof(b), TR(S_BAT_VAL), sd.vbat, sd.batPct, sd.charging ? TR(S_CHARGING) : TR(S_NOT_CHARGING)); row(TR(S_K_BAT), b);
  snprintf(b, sizeof(b), TR(S_WIFI_VAL), cfg.ssid, st.ip ? st.ip : "-", st.rssi); row(TR(S_K_WIFI), b);
  row(TR(S_K_SERVER), cfg.serverUrl);
  snprintf(b, sizeof(b), TR(S_TLS_VAL), cfg.tlsMode == 0 ? TR(S_TLS_INSECURE) : cfg.tlsMode == 1 ? TR(S_TLS_FP) : TR(S_TLS_CA), rtc.tlsSessionOk ? TR(S_YES) : TR(S_NO)); row(TR(S_K_TLS), b);
  snprintf(b, sizeof(b), TR(S_CLOCK_VAL), rtc.rtcChipOk ? TR(S_CLK_CHIP) : TR(S_CLK_SOFT), rtc.ntpDoneDay ? TR(S_NTP_DONE) : TR(S_NTP_NONE), rtc.ntpDoneDay, rtc.pollMinutes ? rtc.pollMinutes : cfg.pollMinutes); row(TR(S_K_CLOCK), b);
  snprintf(b, sizeof(b), TR(S_FAILS_VAL), rtc.wifiFail, rtc.portalFail, rtc.netFail, rtc.shtFail); row(TR(S_K_FAILS), b);
  snprintf(b, sizeof(b), TR(S_NIGHT_VAL), cfg.nightStartHour, cfg.nightEndHour, cfg.nightPollMinutes, cfg.nightTopbar ? TR(S_ON) : TR(S_OFF)); row(TR(S_K_NIGHT), b);
  drawStatusLine(d, st, H - 7);
}

// ---------- Calendar page (coordinates in preview/mock_cal_weather.py) ----------
static void drawCalendarPage(const Dash& d, const UiStatus& st, const ClockNow& t) {
  fg(BLACK);
  if (!t.valid) { font(F_TXT); txtCenter(W / 2, 150, TR(S_NO_CLOCK)); drawStatusLine(d, st, H - 7, TR(S_T_CALENDAR)); return; }
  const int COLW = 57, ROW = 32, Y0 = TOP_H + 23;
  font(F_TXT);
  for (int i = 0; i < 7; i++) txtCenter(COLW * i + COLW / 2 + 1, TOP_H + 16, i18nWeekday((i + 1) % 7, true));   // the header starts on Monday
  display.drawFastHLine(0, TOP_H + 21, W, BLACK);
  int firstWd = (weekdayOf(t.year, t.month, 1) + 6) % 7;     // 0 = Monday
  int n = daysInMonth(t.year, t.month); char b[8];
  for (int day = 1; day <= n; day++) {
    int idx = firstWd + day - 1, r = idx / 7, c = idx % 7;
    int cx = COLW * c + COLW / 2 + 1, ry = Y0 + ROW * r;
    snprintf(b, sizeof(b), "%d", day);
    font(F_TXT);
    if (day == t.day) { display.fillCircle(cx, ry + 10, 11, BLACK); fg(WHITE); txtCenter(cx, ry + 15, b); fg(BLACK); }
    else txtCenter(cx, ry + 15, b);
    if (!i18nEn()) { font(F_SM); txtCenter(cx, ry + 30, lunarCellText(t.year, t.month, day)); }   // the lunar line is only shown in the Chinese UI
  }
  LunarDate l; char s[64];
  // The Chinese and English titles take different arguments (Chinese also carries the lunar date), so branch
  // here instead of going through TR()
  if (i18nEn()) snprintf(s, sizeof(s), "%s %d", i18nMonthName(t.month), t.year);
  else if (lunarFromSolar(t.year, t.month, t.day, l)) snprintf(s, sizeof(s), "%d年%d月   农历%s%s", t.year, t.month, lunarMonthName(l.month, l.leap), lunarDayName(l.day));
  else snprintf(s, sizeof(s), "%d年%d月", t.year, t.month);
  drawStatusLine(d, st, H - 7, s);
}

// ---------- Weather page ----------
static const uint8_t* wxIcon(uint8_t code) {   // Seniverse weather code -> icon (same mapping as the stock firmware's display_tbpd)
  switch (code) {
    case 0: case 2: return WX_qt;      case 1: case 3: return WX_qt_ws;
    case 4: case 5: case 7: return WX_dy; case 6: case 8: return WX_dy_ws;
    case 9: return WX_yt;   case 10: return WX_zheny; case 11: return WX_lzy; case 12: return WX_lzybbb;
    case 13: return WX_xy;  case 14: return WX_zhongy; case 15: return WX_dayu; case 16: return WX_by;
    case 17: return WX_dby; case 18: return WX_tdby; case 19: case 37: return WX_dongy; case 20: return WX_yjx;
    case 21: return WX_zhenx; case 22: return WX_xx; case 23: return WX_zhongx; case 24: return WX_dx; case 25: return WX_bx;
    case 26: return WX_fc;  case 27: return WX_ys; case 28: case 29: return WX_scb; case 30: return WX_w; case 31: return WX_m;
    case 32: case 33: return WX_f; case 34: return WX_jf; case 35: case 38: return WX_rdfb; case 36: return WX_ljf;
    default: return WX_wz;
  }
}
static void drawWeatherPage(const Dash& d, const UiStatus& st) {
  fg(BLACK);
  const Weather& w = d.wx;
  if (!d.valid || !w.valid) {
    font(F_TXT); txtCenter(W / 2, 140, d.valid ? TR(S_WX_UNCONFIGURED) : TR(S_WAIT_SERVER));
    font(F_SM); txtCenter(W / 2, 165, d.valid ? TR(S_WX_HINT) : cfg.serverUrl);
    drawStatusLine(d, st, H - 7, TR(S_T_WEATHER)); return;
  }
  char b[48];
  font(F_TXT); txt(8, TOP_H + 22, w.city);
  font(F_TIME); snprintf(b, sizeof(b), "%d", w.temp); txt(6, TOP_H + 68, b); int x = 6 + tw(b) + 4;
  font(F_NAME); txt(x, TOP_H + 68, "°C");
  display.drawInvertedBitmap(8, TOP_H + 76, wxIcon(w.code), 45, 45, BLACK);
  font(F_TXT); txt(60, TOP_H + 106, i18nWeatherText(w.code, w.text));
  font(F_SM);
  if (w.lo > -99 && w.hi > -99) { snprintf(b, sizeof(b), "%d°C / %d°C", w.lo, w.hi); txt(8, TOP_H + 134, b); }
  String s;
  if (w.humi > -99) { snprintf(b, sizeof(b), TR(S_HUMIDITY), w.humi); s += b; }
  if (w.windDir[0]) { s += i18nWindDir(w.windDir); s += " "; s += i18nWindScale(w.windScale); }
  txt(8, TOP_H + 154, s.c_str());
  if (w.sunrise[0]) { snprintf(b, sizeof(b), TR(S_SUN_TIMES), w.sunrise, w.sunset); txt(8, TOP_H + 174, b); }
  for (uint8_t i = 0; i < w.nDays && i < 4; i++) {
    const WxDay& dy = w.days[i]; int cx = 225 + 50 * i;
    // In English all four columns derive the weekday from the local date ("Tomorrow" is 66 px, leaving only 3 px
    // to its neighbour - too tight); Chinese uses the single-character wk from the server
    if (i18nEn()) txtCenter(cx, TOP_H + 26, i18nWeekday((g_now.weekday + i + 1) % 7, true));
    else if (i == 0) txtCenter(cx, TOP_H + 26, "明天");
    else { snprintf(b, sizeof(b), "周%s", dy.wk); txtCenter(cx, TOP_H + 26, b); }
    txtCenter(cx, TOP_H + 44, dy.date);
    display.drawInvertedBitmap(cx - 22, TOP_H + 50, wxIcon(dy.code), 45, 45, BLACK);
    txtCenter(cx, TOP_H + 112, i18nWeatherText(dy.code, dy.text, true));
    snprintf(b, sizeof(b), "%d/%d°", dy.lo, dy.hi); txtCenter(cx, TOP_H + 130, b);
  }
  if (w.tip[0]) { display.drawFastHLine(20, TOP_H + 186, W - 40, BLACK); font(F_TXT); txtCenter(W / 2, TOP_H + 212, w.tip); }
  snprintf(b, sizeof(b), TR(S_WX_UPDATED), w.updated); drawStatusLine(d, st, H - 7, b);
}

static void drawContent(const Dash& d, const UiStatus& st, Page page) {
  display.fillRect(0, TOP_H + 1, W, H - TOP_H - 1, WHITE);
  switch (page) {
    case PAGE_CALENDAR: drawCalendarPage(d, st, g_now); break;
    case PAGE_WEATHER:  drawWeatherPage(d, st); break;
    case PAGE_SERVERS: drawServersPage(d, st); break;
    case PAGE_CLAUDE:  drawClaudePage(d, st); break;
    case PAGE_DEVICE:  drawDevicePage(d, st, *g_sd); break;
    default: drawDashboard(d, st); break;
  }
}

template <typename F> static void paged(F draw) { display.firstPage(); do { draw(); keysPoll(); } while (display.nextPage()); }

void uiDrawTopBarOnly(const ClockNow& t, const SensorData& sd) {
  g_sd = &sd; g_now = t;
  uint32_t t0 = millis();
  display.setPartialWindow(0, 0, W, TOP_H + 1);
  paged([&] { drawTopBar(t, sd); });
  rtc.partialCount++;
  LOG("ui: topbar partial %lu ms", millis() - t0);
}
// Content update / page flip: like the stock firmware's clock mode, do a partial refresh over a full-screen
// window (the top bar is repainted along with it)
void uiDrawContentOnly(const Dash& d, const UiStatus& st, Page page) {
  static SensorData dummy = {}; if (!g_sd) g_sd = &dummy;
  uint32_t t0 = millis();
  display.init(0, false, 10, false);        // the stock firmware re-inits before every partial refresh
  display.setPartialWindow(0, 0, W, H);
  paged([&] { drawTopBar(g_now, *g_sd); drawContent(d, st, page); });
  rtc.partialCount++;
  LOG("ui: content partial page=%d %lu ms", page, millis() - t0);
}
void uiClearStatusGhost() {   // refresh only the status-line area (H-24..H) to clear the ghost of the key hints
  display.setPartialWindow(0, H - 24, W, 24);
  paged([&] { display.fillScreen(BLACK); });
  paged([&] { display.fillScreen(WHITE); });
}
static void bwFlash() {   // the stock firmware's BW_refresh(): one black pass, one white pass
  display.setPartialWindow(0, 0, W, H);
  paged([&] { display.fillScreen(BLACK); });
  paged([&] { display.fillScreen(WHITE); });
}
void uiDrawAll(const ClockNow& t, const SensorData& sd, const Dash& d, const UiStatus& st, Page page) {
  g_sd = &sd; g_now = t;
  uint32_t t0 = millis();
  if (g_fullPending) { bwFlash(); g_fullPending = false; }
  display.setPartialWindow(0, 0, W, H);
  paged([&] { drawTopBar(t, sd); drawContent(d, st, page); });
  LOG("ui: full draw %lu ms", millis() - t0);
}
void uiMessage(const char* l1, const char* l2, const char* l3) {
  display.init(0, false, 10, false);
  display.setPartialWindow(0, 0, W, H);
  paged([&] {
    display.fillScreen(WHITE); fg(BLACK); font(F_TXT);
    txtCenter(W / 2, 120, l1);
    font(F_SM);
    if (l2) txtCenter(W / 2, 150, l2);
    if (l3) txtCenter(W / 2, 172, l3);
  });
}
void uiPortalScreen(const char* ssid, const char* pass, const char* ip, const char* status) {
  display.init(0, false, 10, false);
  display.setPartialWindow(0, 0, W, H);
  paged([&] {
    display.fillScreen(WHITE); fg(BLACK); char b[64];
    font(F_TXT); txtCenter(W / 2, 40, TR(S_PORTAL_TITLE));
    font(F_SM);
    snprintf(b, sizeof(b), TR(S_PORTAL_L1), ssid); txt(20, 80, b);
    snprintf(b, sizeof(b), TR(S_PORTAL_L2), pass); txt(20, 100, b);
    txt(20, 130, TR(S_PORTAL_L3));
    snprintf(b, sizeof(b), TR(S_PORTAL_L4), ip); txt(20, 150, b);
    txt(20, 180, TR(S_PORTAL_L5));
    display.drawFastHLine(0, 210, W, BLACK);
    txt(20, 240, status ? status : "");
    txt(20, 285, TR(S_PORTAL_L6));
  });
}
