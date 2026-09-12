// Host-side test: c++ -std=c++17 -I.. test_lunar.cpp ../lunar.cpp && ./a.out lunar_expect.csv
#include "lunar.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
  FILE* f = fopen(argc > 1 ? argv[1] : "lunar_expect.csv", "r"); if (!f) { puts("no csv"); return 2; }
  int y, m, d, lm, ll, ld, t, n = 0, bad = 0;
  while (fscanf(f, "%d,%d,%d,%d,%d,%d,%d\n", &y, &m, &d, &lm, &ll, &ld, &t) == 7) {
    LunarDate l; bool ok = lunarFromSolar(y, m, d, l); n++;
    if (y == 2019 && m < 3) continue;   // dates before Chinese New Year 2019 belong to lunar year 2018, which is not in the table
    if (!ok || l.month != lm || (int)l.leap != ll || l.day != ld || l.term != t) {
      if (bad++ < 10) printf("MISMATCH %d-%d-%d got ok=%d %d/%d/%d t%d want %d/%d/%d t%d\n", y, m, d, ok, l.month, l.leap, l.day, l.term, lm, ll, ld, t);
    }
  }
  printf("%d days, %d mismatches\n", n, bad);
  printf("2026-08-29 -> %s | 08-07 %s | 08-19 %s | 08-23 %s | 2026-02-16 %s | 02-17 %s | 05-10 %s | 01-01 %s\n",
    lunarCellText(2026,8,29), lunarCellText(2026,8,7), lunarCellText(2026,8,19), lunarCellText(2026,8,23),
    lunarCellText(2026,2,16), lunarCellText(2026,2,17), lunarCellText(2026,5,10), lunarCellText(2026,1,1));
  printf("weekday 2026-08-29 = %d (want 6)\n", weekdayOf(2026,8,29));
  return bad ? 1 : 0;
}
