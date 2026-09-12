// SHT30 temperature/humidity, battery voltage/percentage, charging inference (ported from the stock firmware's Get_bat_vcc.ino)
#pragma once
#include <Arduino.h>

struct SensorData {
  bool  shtOk;
  float temp, humi;
  float vbat;          // V
  uint8_t batPct;      // 0-100
  uint8_t batBars;     // 1-3
  bool  charging;
};

void sensorsRead(SensorData& d, bool measureBattery);   // measureBattery=false: ADC is unreliable (RF-off wake), reuse the rtc cache
float batteryVoltageNow();         // instantaneous voltage (average of 30 reads)
uint8_t batteryPercent(float v);
