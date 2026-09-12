#include "clock.h"
#include "config.h"
#include "settings.h"
#include "rtc_mem.h"
#include "i18n.h"
#include <Wire.h>
#include <BL8025_RTC.h>
#include <TimeLib.h>
#include <time.h>
#include <sys/time.h>

static BL8025_RTC chip;
const char* clockWeekdayName(uint8_t wd) { return i18nWeekday(wd, false); }

uint8_t clockWeekday(uint16_t y, uint8_t m, uint8_t d) {  // Zeller, 0=Sunday
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

uint32_t clockSecondsOfDay(const ClockNow& t) { return t.hour * 3600UL + t.minute * 60UL + t.second; }

static bool tmValid(const tmElements_t& tm) {
  int y = tmYearToCalendar(tm.Year);
  if (y < 2020 || y > 2099) return false;
  if (tm.Month < 1 || tm.Month > 12) return false;
  if (tm.Day < 1 || tm.Day > 31) return false;
  if (tm.Hour > 23 || tm.Minute > 59 || tm.Second > 59) return false;
  return true;
}

void clockBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClockStretchLimit(2000);
  LOG("clock: begin (type known=%d)", cfg.rtcTypeKnown);
  if (!cfg.rtcTypeKnown) {
    if (clockChipDetect()) { cfg.rtcTypeKnown = 1; settingsSave(); }
  }
}

// Stock firmware ClockChipCheck(): write a test time, read it back with both decodings; whichever matches is the chip type
bool clockChipDetect() {
  tmElements_t t1; t1.Year = CalendarYrToTm(2033); t1.Month = 3; t1.Day = 3; t1.Hour = 3; t1.Minute = 3; t1.Second = 3;
  LOG("clock: detect write");
  chip.write(t1); delay(2);
  LOG("clock: detect read");
  tmElements_t a = chip.read(0), b = chip.read(1);
  LOG("clock: a=%d/%d/%d %d:%d:%d b=%d/%d/%d %d:%d:%d", a.Year, a.Month, a.Day, a.Hour, a.Minute, a.Second, b.Year, b.Month, b.Day, b.Hour, b.Minute, b.Second);
  auto same = [&](const tmElements_t& x) {
    return x.Year == t1.Year && x.Month == t1.Month && x.Hour == t1.Hour && x.Minute == t1.Minute && x.Second == t1.Second;
  };
  if (same(a)) { cfg.rtcType = 0; LOG("rtc chip: BL type"); return true; }
  if (same(b)) { cfg.rtcType = 1; LOG("rtc chip: RX type"); return true; }
  LOG("rtc chip: not found");
  return false;
}

bool clockChipRead(ClockNow& o) {
  tmElements_t tm = chip.read(cfg.rtcType);
  if (!tmValid(tm)) { o.valid = false; return false; }
  o.year = tmYearToCalendar(tm.Year); o.month = tm.Month; o.day = tm.Day;
  o.hour = tm.Hour; o.minute = tm.Minute; o.second = tm.Second;
  o.weekday = clockWeekday(o.year, o.month, o.day);
  o.valid = true;
  return true;
}

bool clockChipWrite(const ClockNow& t) {
  tmElements_t tm; tm.Year = CalendarYrToTm(t.year); tm.Month = t.month; tm.Day = t.day;
  tm.Hour = t.hour; tm.Minute = t.minute; tm.Second = t.second;
  chip.write(tm); delay(2);
  ClockNow back;
  return clockChipRead(back) && back.hour == t.hour && back.minute == t.minute;
}

static uint8_t daysInMonth(uint16_t y, uint8_t m) {
  static const uint8_t d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

void clockSoftTick() {  // the stock firmware's "compute the time ourselves" branch
  if (rtc.hour > 23) return;
  rtc.second = 0;
  if (++rtc.minute >= 60) {
    rtc.minute = 0;
    if (++rtc.hour >= 24) {
      rtc.hour = 0;
      if (++rtc.day > daysInMonth(rtc.year, rtc.month)) { rtc.day = 1; if (++rtc.month > 12) { rtc.month = 1; rtc.year++; } }
    }
  }
}

bool clockGet(ClockNow& o) {
  if (cfg.rtcTypeKnown && clockChipRead(o)) {
    rtc.rtcChipOk = 1;
    rtc.hour = o.hour; rtc.minute = o.minute; rtc.second = o.second;
    rtc.year = o.year; rtc.month = o.month; rtc.day = o.day;
    return true;
  }
  rtc.rtcChipOk = 0;
  if (rtc.hour > 23 || rtc.year < 2020) { o.valid = false; return false; }
  o.year = rtc.year; o.month = rtc.month; o.day = rtc.day;
  o.hour = rtc.hour; o.minute = rtc.minute; o.second = rtc.second;
  o.weekday = clockWeekday(o.year, o.month, o.day);
  o.valid = true;
  return true;
}

bool clockNtpSync(ClockNow& o) {
  configTime(NTP_TZ_SECONDS, 0, "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");
  uint32_t t0 = millis();
  time_t now = 0;
  while (millis() - t0 < 6000) { now = time(nullptr); if (now > 1600000000) break; delay(50); }
  if (now < 1600000000) { LOG("ntp: timeout"); return false; }
  struct tm* lt = localtime(&now);
  o.year = lt->tm_year + 1900; o.month = lt->tm_mon + 1; o.day = lt->tm_mday;
  o.hour = lt->tm_hour; o.minute = lt->tm_min; o.second = lt->tm_sec; o.weekday = lt->tm_wday; o.valid = true;
  LOG("ntp: %04d-%02d-%02d %02d:%02d:%02d", o.year, o.month, o.day, o.hour, o.minute, o.second);
  rtc.hour = o.hour; rtc.minute = o.minute; rtc.second = o.second; rtc.year = o.year; rtc.month = o.month; rtc.day = o.day;
  if (cfg.rtcTypeKnown) {
    if (clockChipWrite(o)) { rtc.rtcChipOk = 1; LOG("ntp: chip written"); }
    else { rtc.rtcChipOk = 0; LOG("ntp: chip write verify failed"); }
  }
  rtc.ntpDoneDay = o.day;
  return true;
}

void clockToSystem(const ClockNow& t) {
  if (!t.valid) return;
  struct tm tmv = {}; tmv.tm_year = t.year - 1900; tmv.tm_mon = t.month - 1; tmv.tm_mday = t.day;
  tmv.tm_hour = t.hour; tmv.tm_min = t.minute; tmv.tm_sec = t.second;
  time_t epoch = mktime(&tmv) - NTP_TZ_SECONDS;   // mktime treats it as UTC (no TZ set)
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, nullptr);
}
