#include "i18n.h"
#include "settings.h"

// String table: both columns point at literals in PROGMEM, and the table itself lives in PROGMEM too
#define I18N_DECL(id, zh, en) static const char id##_z[] PROGMEM = zh; static const char id##_e[] PROGMEM = en;
I18N_STRINGS(I18N_DECL)
#define I18N_ROW(id, zh, en) { id##_z, id##_e },
static const char* const I18N_TBL[STR_COUNT][2] PROGMEM = { I18N_STRINGS(I18N_ROW) };

#define TR_SLOTS 6
#define TR_BUF   72
static char    g_tr[TR_SLOTS][TR_BUF];
static uint8_t g_slot = 0;

static char* slot() { char* d = g_tr[g_slot]; g_slot = (uint8_t)((g_slot + 1) % TR_SLOTS); return d; }
static const char* fromP(const char* p) { char* d = slot(); strncpy_P(d, p, TR_BUF - 1); d[TR_BUF - 1] = 0; return d; }
// take entry n from a NUL-separated PROGMEM string table
static const char* nthP(const char* blob, uint8_t n) {
  while (n--) { while (pgm_read_byte(blob)) blob++; blob++; }
  return fromP(blob);
}

bool i18nEn() { return cfg.lang == LANG_EN; }

const char* TR(uint16_t id) {
  if (id >= STR_COUNT) return "";
  return fromP((const char*)pgm_read_ptr(&I18N_TBL[id][i18nEn() ? 1 : 0]));
}

// ---------- Weekdays / months ----------
static const char WD_ZH_FULL[]  PROGMEM = "周日周一周二周三周四周五周六";   // 6 bytes per entry
static const char WD_ZH_SHORT[] PROGMEM = "日一二三四五六";                 // 3 bytes per entry
static const char WD_EN[]       PROGMEM = "SunMonTueWedThuFriSat";          // 3 bytes per entry
static const char MONTHS_EN[]   PROGMEM = "January\0February\0March\0April\0May\0June\0July\0August\0September\0October\0November\0December";

static const char* fixedP(const char* blob, uint8_t idx, uint8_t n) {
  char* d = slot(); memcpy_P(d, blob + (uint16_t)idx * n, n); d[n] = 0; return d;
}
const char* i18nWeekday(uint8_t wd, bool shortForm) {
  wd %= 7;
  if (i18nEn()) return fixedP(WD_EN, wd, 3);
  return shortForm ? fixedP(WD_ZH_SHORT, wd, 3) : fixedP(WD_ZH_FULL, wd, 6);
}
const char* i18nMonthName(uint8_t m) { return (m >= 1 && m <= 12) ? nthP(MONTHS_EN, m - 1) : ""; }

// ---------- Weather ----------
// Short English names for Seniverse weather codes 0-38 (the 4-day forecast columns are only 50 px, so keep them 8-10 chars)
static const char WX_EN[] PROGMEM =
  "Sunny\0Clear\0Fair\0Fair\0Cloudy\0P.Cloudy\0P.Cloudy\0M.Cloudy\0M.Cloudy\0Overcast\0"
  "Shower\0T-storm\0Hail\0Lt Rain\0Rain\0Hvy Rain\0Storm\0Storm+\0Storm++\0Ice Rain\0"
  "Sleet\0Snow Sh.\0Lt Snow\0Snow\0Hvy Snow\0Blizzard\0Dust\0Sand\0Duststorm\0Sandstorm\0"
  "Fog\0Haze\0Windy\0Gale\0Hurricane\0Trop.Storm\0Tornado\0Cold\0Hot";
// The four forecast columns are only 50 px wide, so use an even shorter set (measured in noto14, widest 55 px)
static const char WX_EN_SHORT[] PROGMEM =
  "Sunny\0Clear\0Fair\0Fair\0Cloudy\0P.Cldy\0P.Cldy\0M.Cldy\0M.Cldy\0Ovcast\0"
  "Shwr\0T-stm\0Hail\0Rain-\0Rain\0Rain+\0Storm\0Storm+\0Deluge\0IceRn\0"
  "Sleet\0SnwShr\0Snow-\0Snow\0Snow+\0Blizrd\0Dust\0Sand\0DstStm\0SndStm\0"
  "Fog\0Haze\0Windy\0Gale\0Hurr.\0TropSt\0Tornado\0Cold\0Hot";
static const char WX_UNKNOWN[] PROGMEM = "Unknown";
static const char WX_UNKNOWN_S[] PROGMEM = "?";

const char* i18nWeatherText(uint8_t code, const char* fromServer, bool shortForm) {
  if (!i18nEn()) return fromServer;
  if (code > 38) return fromP(shortForm ? WX_UNKNOWN_S : WX_UNKNOWN);
  return nthP(shortForm ? WX_EN_SHORT : WX_EN, code);
}

// Wind direction: the server sends Chinese like 「西南」 / 「西南风」 (the field is only 10 bytes, longer values get truncated)
static const char WIND_ZH_EN[] PROGMEM =
  "东\0E\0南\0S\0西\0W\0北\0N\0东北\0NE\0东南\0SE\0西北\0NW\0西南\0SW\0"
  "北东北\0NNE\0东东北\0ENE\0东东南\0ESE\0南东南\0SSE\0南西南\0SSW\0西西南\0WSW\0西西北\0WNW\0北西北\0NNW";

const char* i18nWindDir(const char* zh) {
  if (!i18nEn() || !zh || !zh[0]) return zh;
  if (strncmp(zh, "无持续", 9) == 0) return TR(S_WIND_VARIABLE);
  char key[12]; strncpy(key, zh, sizeof(key) - 1); key[sizeof(key) - 1] = 0;
  size_t n = strlen(key);
  if (n >= 3 && strcmp(key + n - 3, "风") == 0) key[n - 3] = 0;   // strip the trailing 「风」 before the lookup
  for (const char* p = WIND_ZH_EN; pgm_read_byte(p); ) {
    const char* v = p; while (pgm_read_byte(v)) v++; v++;
    if (strcmp_P(key, p) == 0) return fromP(v);
    p = v; while (pgm_read_byte(p)) p++; p++;
  }
  return zh;
}
// "3-4级" -> "3-4 Bft"
const char* i18nWindScale(const char* zh) {
  if (!i18nEn() || !zh || !zh[0]) return zh;
  char* d = slot(); uint8_t k = 0;
  for (const char* s = zh; *s && k < TR_BUF - 6; s++) if ((*s >= '0' && *s <= '9') || *s == '-') d[k++] = *s;
  if (!k) return "";
  strcpy(d + k, " Bft");
  return d;
}
