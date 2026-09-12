// Time: BL8025T/RX8025T chip first, NTP calibration, software tick as fallback (ported from the stock firmware's Clock_8025T.ino / read_RTC_time)
#pragma once
#include <Arduino.h>

struct ClockNow {
  uint16_t year; uint8_t month, day, hour, minute, second, weekday; // weekday 0=Sunday
  bool valid;
};

void     clockBegin();                 // Wire.begin + chip probe (if unknown)
bool     clockChipDetect();            // auto-detect BL/RX decoding, result stored in cfg
bool     clockChipRead(ClockNow& out); // read the chip and sanity-check the value
bool     clockChipWrite(const ClockNow& t);
void     clockSoftTick();              // when the chip is unavailable: minute +1
bool     clockGet(ClockNow& out);      // single entry point: chip, else the software clock
bool     clockNtpSync(ClockNow& out);  // requires WiFi; on success writes back to the chip and RTC memory
void     clockToSystem(const ClockNow& t); // settimeofday, for TLS certificate validation
uint8_t  clockWeekday(uint16_t y, uint8_t m, uint8_t d);
uint32_t clockSecondsOfDay(const ClockNow& t);
const char* clockWeekdayName(uint8_t wd);
