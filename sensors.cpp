#include "sensors.h"
#include "config.h"
#include "rtc_mem.h"
#include <Wire.h>
#include <ClosedCube_SHT31D.h>

static ClosedCube_SHT31D sht;

float batteryVoltageNow() {
  pinMode(BAT_SWITCH_PIN, OUTPUT); digitalWrite(BAT_SWITCH_PIN, 1); delay(1);
  float acc = 0;
  for (uint8_t i = 0; i < 30; i++) acc += analogRead(BAT_ADC_PIN) * BAT_ADC_COEF;
  digitalWrite(BAT_SWITCH_PIN, 0); pinMode(BAT_SWITCH_PIN, INPUT);
  return acc / 30.0f;
}

uint8_t batteryPercent(float v) {   // quartic polynomial from the stock firmware's getBatVolBfb
  double b = 497.50976 * v * v * v * v - 7442.07254 * v * v * v + 41515.70648 * v * v - 102249.34377 * v + 93770.99821;
  if (b > 100) b = 100; else if (b < 0) b = 3;
  return (uint8_t)(b + 0.5);
}

static bool inferCharging(float v) {
  uint16_t mv = (uint16_t)(v * 1000);
  // ring history (last 5 wakeups)
  uint16_t oldest = rtc.vHist[rtc.vIdx % 5];      // the one about to be overwritten = oldest
  rtc.vHist[rtc.vIdx % 5] = mv; rtc.vIdx = (rtc.vIdx + 1) % 5;
  uint8_t cnt = 0; uint16_t tmp[5];
  for (uint8_t i = 0; i < 5; i++) if (rtc.vHist[i]) tmp[cnt++] = rtc.vHist[i];
  for (uint8_t i = 1; i < cnt; i++) for (uint8_t j = i; j > 0 && tmp[j - 1] > tmp[j]; j--) { uint16_t t = tmp[j]; tmp[j] = tmp[j - 1]; tmp[j - 1] = t; }
  uint16_t med = cnt ? tmp[cnt / 2] : mv;          // median rejects single-sample ADC noise
  bool rising = cnt == 5 && oldest && (int)med - (int)oldest >= (int)(BAT_CHG_RISE_V * 1000);
  bool prev = rtc.lastCharging;
  if (rising) return true;
  if (med >= (uint16_t)(BAT_CHG_FULL_V * 1000)) return true;                 // >=4.19 V constant-voltage stage
  if (prev && med >= (uint16_t)((BAT_CHG_FULL_V - 0.03f) * 1000)) return true; // hysteresis 4.29-4.32
  return false;
}

void sensorsRead(SensorData& d, bool measureBattery) {
  // same as the stock firmware: drive GPIO12 high to power the SHT30 and enable battery measurement
  pinMode(BAT_SWITCH_PIN, OUTPUT); digitalWrite(BAT_SWITCH_PIN, 1); delay(2);
  Wire.begin(I2C_SDA, I2C_SCL);
  sht.begin(SHT30_ADDR);
  SHT31D r = sht.readTempAndHumidity(SHT3XD_REPEATABILITY_LOW, SHT3XD_MODE_CLOCK_STRETCH, 50);
  if (r.error == SHT3XD_NO_ERROR) { d.shtOk = true; d.temp = r.t; d.humi = r.rh; rtc.shtFail = 0; }
  else { d.shtOk = false; d.temp = d.humi = 0; if (rtc.shtFail < 250) rtc.shtFail++; LOG("sht30 err %d", r.error); }
  if (measureBattery || !rtc.lastVbatMv) {
    float acc = 0;
    for (uint8_t i = 0; i < 30; i++) acc += analogRead(BAT_ADC_PIN) * BAT_ADC_COEF;
    d.vbat = acc / 30.0f;
    rtc.lastVbatMv = (uint16_t)(d.vbat * 1000);
    d.charging = inferCharging(d.vbat);
    rtc.lastCharging = d.charging;
  } else {
    d.vbat = rtc.lastVbatMv / 1000.0f;   // with RF off the ESP8266 ADC reference is inaccurate, reuse the last trusted value
    d.charging = rtc.lastCharging;
  }
  digitalWrite(BAT_SWITCH_PIN, 0); pinMode(BAT_SWITCH_PIN, INPUT);
  d.batPct = batteryPercent(d.vbat);
  d.batBars = d.vbat > 3.7f ? 3 : (d.vbat > 3.5f ? 2 : 1);
  LOG("sensors: t=%.1f h=%.1f v=%.2f pct=%d chg=%d", d.temp, d.humi, d.vbat, d.batPct, d.charging);
}
