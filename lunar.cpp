#include "lunar.h"
#include <string.h>
#include <stdio.h>
#ifdef ARDUINO
#include <Arduino.h>
#else   // host-side tests (preview/test_lunar.cpp)
#define PROGMEM
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_byte(p)  (*(const uint8_t*)(p))
#endif
#include "lunar_tab.h"

static const char* const DAY_NAMES[30] = {
  "初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
  "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
  "廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十" };
static const char* const MONTH_NAMES[12] = { "正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月" };
static const char* const TERM_NAMES[24] = {
  "小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨","立夏","小满","芒种","夏至",
  "小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降","立冬","小雪","大雪","冬至" };

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
int daysInMonth(int y, int m) {
  static const uint8_t dm[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  return (m == 2 && isLeapYear(y)) ? 29 : dm[m - 1];
}
int weekdayOf(int y, int m, int d) {   // Zeller (Sakamoto)
  static const int t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static int dayOfYear(int y, int m, int d) { int n = d; for (int i = 1; i < m; i++) n += daysInMonth(y, i); return n; }
static int daysBetween(int y1, int m1, int d1, int y2, int m2, int d2) {   // (y2,m2,d2) - (y1,m1,d1)
  int n = dayOfYear(y2, m2, d2) - dayOfYear(y1, m1, d1);
  for (int y = y1; y < y2; y++) n += isLeapYear(y) ? 366 : 365;
  for (int y = y2; y < y1; y++) n -= isLeapYear(y) ? 366 : 365;
  return n;
}

bool lunarFromSolar(int y, int m, int d, LunarDate& out) {
  out.term = -1;
  if (y < LUNAR_YEAR0 || y >= LUNAR_YEAR0 + LUNAR_YEARS) return false;
  // solar term (looked up by Gregorian month)
  const uint8_t* tp = TERM_TAB[y - LUNAR_YEAR0];
  if (pgm_read_byte(tp + (m - 1) * 2) == d) out.term = (m - 1) * 2;
  else if (pgm_read_byte(tp + (m - 1) * 2 + 1) == d) out.term = (m - 1) * 2 + 1;
  // lunar year: dates before this year's lunar new year belong to the previous lunar year
  int ly = y;
  uint32_t v = pgm_read_dword(&LUNAR_TAB[ly - LUNAR_YEAR0]);
  int nyM = (v >> 22) & 0xF, nyD = (v >> 17) & 0x1F;
  if (m < nyM || (m == nyM && d < nyD)) {
    ly--;
    if (ly < LUNAR_YEAR0) return false;
    v = pgm_read_dword(&LUNAR_TAB[ly - LUNAR_YEAR0]);
    nyM = (v >> 22) & 0xF; nyD = (v >> 17) & 0x1F;
  }
  int off = daysBetween(ly, nyM, nyD, y, m, d);
  int leap = (v >> 13) & 0xF;
  int month = 1; bool isLeap = false;
  for (int i = 0; i < 13; i++) {
    int len = (v & (1UL << i)) ? 30 : 29;
    if (off < len) { out.month = month; out.leap = isLeap; out.day = off + 1; return true; }
    off -= len;
    if (isLeap) { isLeap = false; month++; }
    else if (leap && month == leap) isLeap = true;
    else month++;
  }
  return false;
}

const char* lunarMonthName(uint8_t month, bool leap) {
  static char b[12];
  if (month < 1 || month > 12) return "";
  if (!leap) return MONTH_NAMES[month - 1];
  snprintf(b, sizeof(b), "闰%s", MONTH_NAMES[month - 1]);
  return b;
}
const char* lunarDayName(uint8_t day) { return (day >= 1 && day <= 30) ? DAY_NAMES[day - 1] : ""; }
const char* lunarTermName(int8_t term) { return (term >= 0 && term < 24) ? TERM_NAMES[term] : ""; }

struct Fest { uint8_t m, d; const char* name; };
static const Fest SOLAR_FEST[] = {
  {1,1,"元旦"}, {2,14,"情人节"}, {3,8,"妇女节"}, {3,12,"植树节"}, {5,1,"劳动节"}, {5,4,"青年节"}, {6,1,"儿童节"},
  {7,1,"建党节"}, {8,1,"建军节"}, {9,10,"教师节"}, {10,1,"国庆节"}, {12,24,"平安夜"}, {12,25,"圣诞节"} };
static const Fest LUNAR_FEST[] = {
  {1,1,"春节"}, {1,15,"元宵节"}, {2,2,"龙抬头"}, {5,5,"端午节"}, {7,7,"七夕节"}, {7,15,"中元节"},
  {8,15,"中秋节"}, {9,9,"重阳节"}, {12,8,"腊八节"}, {12,23,"小年"} };

const char* lunarCellText(int y, int m, int d) {
  LunarDate l;
  bool ok = lunarFromSolar(y, m, d, l);
  if (ok && !l.leap) {
    if (l.month == 12) {   // New Year's Eve: last day of the 12th lunar month
      LunarDate n; int y2 = y, m2 = m, d2 = d + 1;
      if (d2 > daysInMonth(y2, m2)) { d2 = 1; if (++m2 > 12) { m2 = 1; y2++; } }
      if (lunarFromSolar(y2, m2, d2, n) && n.month == 1 && n.day == 1) return "除夕";
    }
    for (const Fest& f : LUNAR_FEST) if (f.m == l.month && f.d == l.day) return f.name;
  }
  for (const Fest& f : SOLAR_FEST) if (f.m == m && f.d == d) return f.name;
  int wd = weekdayOf(y, m, d);
  if (m == 5 && wd == 0 && d >= 8 && d <= 14) return "母亲节";
  if (m == 6 && wd == 0 && d >= 15 && d <= 21) return "父亲节";
  if (!ok) return "";
  if (l.term >= 0) return lunarTermName(l.term);
  if (l.day == 1) return lunarMonthName(l.month, l.leap);
  return lunarDayName(l.day);
}
