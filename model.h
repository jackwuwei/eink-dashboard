// Device-side model of the server JSON (field names mirror server/app/eink.py)
#pragma once
#include <Arduino.h>

struct DashServer { char name[24]; bool ok; int16_t cpu, mem, temp; char up[20]; char detail[28]; uint8_t nExtra; char extraK[3][16]; char extraV[3][16]; };
struct DashLimit  { char label[24]; uint8_t pct; char reset[16]; };
struct DashEnv    { char icon[8]; char label[16]; char value[16]; };
// Weather (server weather.py; codes are Seniverse weather codes, the device picks an icon per code)
struct WxDay   { char date[6]; char wk[4]; uint8_t code; char text[14]; int8_t lo, hi; };
struct Weather {
  bool valid;
  char city[12]; int8_t temp; uint8_t code; char text[14];
  int8_t lo, hi, humi;            // -99 = missing
  char windDir[10], windScale[8];
  char sunrise[6], sunset[6];
  char tip[80]; char updated[6];
  uint8_t nDays; WxDay days[4];   // starting from tomorrow
};

struct Dash {
  bool valid;
  char rev[12];
  uint16_t nextPollSec;
  uint8_t alert;
  char ts[8];             // server render time HH:MM
  uint8_t nServers; DashServer servers[6];
  uint8_t nLimits;  DashLimit  limits[4];
  char claudeNote[40];
  uint8_t nEnv;     DashEnv env[4];
  char title[24];
  Weather wx;
};

extern char g_jsonBuf[];   // shared HTTP body / JSON buffer (single global, saves RAM)

bool modelParse(const char* json, Dash& d);   // parse (returns false on failure, d.valid=false)
bool modelLoadCached(Dash& d);                // restore from LittleFS /last.json
void modelSaveCached(const char* json, size_t len);
