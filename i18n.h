// Bilingual (Chinese/English) UI string table. The language lives in cfg.lang (switched on the provisioning
// page) and the strings themselves live in PROGMEM; TR() copies one into a rotating buffer and returns a
// plain const char* (usable directly by u8g2 / snprintf). That saves about 1.5 KB over keeping both sets in
// RAM -- this board is far tighter on RAM than on flash.
// Note: at most TR_SLOTS(6) TR() calls may appear in one expression, the format string itself counting as
// one; a single string is limited to 71 bytes.
#pragma once
#include <Arduino.h>

enum Lang : uint8_t { LANG_ZH = 0, LANG_EN = 1 };

// X-macro: the enum and the string table expand from one definition, so they can never drift apart.
// English strings are ASCII only (the 32-126 range of the bitmap font); entries that are identical in both
// languages are written twice and the compiler merges the duplicate literals.
#define I18N_STRINGS(X)                                                                                      \
  /* Top bar / status line */                                                                                \
  X(S_TH_UNKNOWN,        "温湿度 ?",                    "T/RH ?")                                            \
  X(S_NET_WIFI,          "WiFi ✕",                      "WiFi ✕")                                            \
  X(S_NET_PORTAL,        "门户认证",                    "Portal auth")                                       \
  X(S_NET_SRV_OFF,       "服务端离线",                  "Server offline")                                    \
  X(S_NET_CERT,          "证书错误",                    "Cert error")                                        \
  X(S_NET_PORTAL_LOGIN,  "门户登录失败",                "Portal login failed")                               \
  X(S_UPDATED_AT,        "更新 %02d:%02d",              "Upd %02d:%02d")                                     \
  X(S_NO_DATA,           "暂无数据",                    "No data")                                           \
  X(S_KEYS_HINT,         "[2]下页 [3]上页",             "[2]Next [3]Prev")                                   \
  /* Dashboard page */                                                                                       \
  X(S_WAIT_SERVER,       "等待服务端数据…",             "Waiting for server...")                             \
  X(S_OFFLINE_DETAIL,    "离线  %s",                    "OFFLINE  %s")                                       \
  X(S_RESET_AT,          "%s 重置",                     "resets %s")                                         \
  /* Detail page titles */                                                                                   \
  X(S_T_SERVERS,         "服务器详情",                  "Servers")                                           \
  X(S_T_CLAUDE,          "Claude 用量",                 "Claude Usage")                                      \
  X(S_T_DEVICE,          "设备信息",                    "Device")                                            \
  X(S_T_CALENDAR,        "日历",                        "Calendar")                                          \
  X(S_T_WEATHER,         "天气",                        "Weather")                                           \
  /* Servers page */                                                                                         \
  X(S_ONLINE,            "在线",                        "Online")                                            \
  X(S_OFFLINE,           "离线",                        "Offline")                                           \
  X(S_MEM,               "内存",                        "RAM")                                               \
  /* Device page: left-column field names */                                                                 \
  X(S_K_FW,              "固件",                        "Firmware")                                          \
  X(S_K_BAT,             "电池",                        "Battery")                                           \
  X(S_K_WIFI,            "WiFi",                        "WiFi")                                              \
  X(S_K_SERVER,          "服务端",                      "Server")                                            \
  X(S_K_TLS,             "TLS",                         "TLS")                                               \
  X(S_K_CLOCK,           "时钟",                        "Clock")                                             \
  X(S_K_FAILS,           "失败计数",                    "Failures")                                          \
  X(S_K_NIGHT,           "夜间",                        "Night")                                             \
  /* Device page: values */                                                                                  \
  X(S_FW_VAL,            "%s   唤醒 %u 次   空闲堆 %u", "%s   wake %u   heap %u")                            \
  X(S_BAT_VAL,           "%.2f V (%d%%)  %s",           "%.2f V (%d%%)  %s")                                 \
  X(S_CHARGING,          "充电中",                      "charging")                                          \
  X(S_NOT_CHARGING,      "未充电",                      "not charging")                                      \
  X(S_WIFI_VAL,          "%s  %s  %d dBm",              "%s  %s  %d dBm")                                    \
  X(S_TLS_INSECURE,      "不验证",                      "insecure")                                          \
  X(S_TLS_FP,            "指纹",                        "fingerprint")                                       \
  X(S_TLS_CA,            "根CA",                        "root CA")                                            \
  X(S_TLS_VAL,           "%s   会话缓存 %s",            "%s   session cache %s")                             \
  X(S_YES,               "有",                          "yes")                                               \
  X(S_NO,                "无",                          "no")                                                \
  X(S_CLK_CHIP,          "芯片正常",                    "RTC chip")                                          \
  X(S_CLK_SOFT,          "软件计时",                    "software")                                          \
  X(S_NTP_DONE,          "已校准",                      "synced")                                            \
  X(S_NTP_NONE,          "未校准",                      "not synced")                                        \
  X(S_CLOCK_VAL,         "%s   NTP %s(%d日)   轮询 %d 分钟", "%s   NTP %s(day %d)   poll %d min")            \
  X(S_FAILS_VAL,         "WiFi %d  门户 %d  网络 %d  温湿度 %d", "WiFi %d  portal %d  net %d  T/RH %d")      \
  X(S_NIGHT_VAL,         "%02d:00–%02d:00 每 %d 分钟   顶栏 %s", "%02d:00-%02d:00 every %d min   topbar %s") \
  X(S_ON,                "刷",                          "on")                                                \
  X(S_OFF,               "不刷",                        "off")                                               \
  /* Calendar page */                                                                                        \
  X(S_NO_CLOCK,          "时间无效",                    "No clock")                                          \
  /* Weather page */                                                                                         \
  X(S_WX_UNCONFIGURED,   "服务端未配置天气",            "Weather not configured")                            \
  X(S_WX_HINT,           "config.yaml → eink.weather",  "config.yaml -> eink.weather")                       \
  X(S_HUMIDITY,          "湿度 %d%%   ",                "Humidity %d%%   ")                                  \
  X(S_SUN_TIMES,         "日出 %s  |  日落 %s",         "Sunrise %s  |  Sunset %s")                          \
  X(S_WX_UPDATED,        "天气   更新 %s",              "Weather   upd %s")                                  \
  X(S_WIND_VARIABLE,     "无持续风向",                  "Variable")                                          \
  /* Full-screen messages */                                                                                 \
  X(S_BAT_DEAD_1,        "电量过低，已休眠",            "Battery too low - sleeping")                        \
  X(S_BAT_DEAD_2,        "请充电后按复位键唤醒",        "Charge, then press reset")                          \
  /* Provisioning screen */                                                                                  \
  X(S_PORTAL_TITLE,      "配网模式",                    "Setup Mode")                                        \
  X(S_PORTAL_L1,         "1. 手机连接热点: %s",         "1. Join the hotspot: %s")                           \
  X(S_PORTAL_L2,         "   密码: %s",                 "   Password: %s")                                   \
  X(S_PORTAL_L3,         "2. 连接后会自动弹出配置页；", "2. The setup page should pop up;")                  \
  X(S_PORTAL_L4,         "   没弹出就用浏览器打开 http://%s", "   if not, open http://%s")                   \
  X(S_PORTAL_L5,         "3. 填 WiFi、服务端地址，点“测试连接”再保存", "3. Fill in WiFi + server, Test, Save") \
  X(S_PORTAL_L6,         "10 分钟无操作自动休眠",       "Sleeps after 10 min idle")                          \
  X(S_PORTAL_FIRST,      "首次使用，请先配置",          "First run - please configure")                      \
  X(S_PORTAL_AGAIN,      "修改配置后保存即可",          "Edit the settings and save")                        \
  X(S_PORTAL_TESTING,    "测试连接中…",                 "Testing connection...")                             \
  X(S_PORTAL_TEST_RES,   "测试: %s",                    "Test: %s")                                          \
  X(S_PORTAL_WIFI_FAIL,  "WiFi 连接失败",               "WiFi connection failed")                            \
  X(S_PORTAL_CONNECTING, "正在连接 %s …",               "Connecting to %s ...")                              \
  X(S_PORTAL_CONNECTED,  "连接成功 IP %s，5 秒后进入看板", "Connected, IP %s - dashboard in 5 s")            \
  X(S_PORTAL_RETRY_PHY,  "换 11%s 模式重试 %s …",       "11%s retry: %s ...")                                \
  X(S_PORTAL_FAILED,     "连接 %s 失败，请重新填写",    "Cannot connect to %s - check settings")             \
  X(S_PORTAL_TIMEOUT_1,  "配网超时，已休眠",            "Setup timed out - sleeping")                        \
  X(S_PORTAL_TIMEOUT_2,  "按住按键3再按复位键重新进入配网", "Hold key 3 and press reset to retry")            \
  /* Units */                                                                                                \
  X(S_DAYS_SUFFIX,       "%.1f天",                      "%.1fd")

#define I18N_ENUM(id, zh, en) id,
enum StrId : uint16_t { I18N_STRINGS(I18N_ENUM) STR_COUNT };
#undef I18N_ENUM

bool        i18nEn();                                   // whether the UI is currently English
const char* TR(uint16_t id);
const char* i18nWeekday(uint8_t wd, bool shortForm);    // wd 0=Sunday; shortForm is for the calendar header
const char* i18nMonthName(uint8_t m);                   // English month name (unused by the Chinese UI)
// English name for a weather code (shortForm is for the 4-day forecast columns, only 50 px wide);
// the Chinese UI uses the server's own text
const char* i18nWeatherText(uint8_t code, const char* fromServer, bool shortForm = false);
const char* i18nWindDir(const char* zh);                // "东北风" -> "NE"; returns the input unchanged if unrecognized
const char* i18nWindScale(const char* zh);              // "3-4级" -> "3-4 Bft"
