#include "rtc_mem.h"
#include "config.h"

RtcState rtc;
static const uint32_t RTC_MAGIC = 0x48444231; // "HDB1"

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}

static_assert(sizeof(RtcState) <= 512, "RtcState too big");
static uint32_t rtcWords[(sizeof(RtcState) + 3) / 4];

bool rtcLoad() {
  ESP.rtcUserMemoryRead(0, rtcWords, sizeof(rtcWords));
  memcpy(&rtc, rtcWords, sizeof(rtc));
  if (rtc.magic != RTC_MAGIC) return false;
  uint32_t c = crc32((const uint8_t*)&rtc, sizeof(rtc) - 4);
  if (c != rtc.crc) return false;
  return true;
}

void rtcSave() {
  rtc.magic = RTC_MAGIC;
  rtc.crc = crc32((const uint8_t*)&rtc, sizeof(rtc) - 4);
  memcpy(rtcWords, &rtc, sizeof(rtc));
  ESP.rtcUserMemoryWrite(0, rtcWords, sizeof(rtcWords));
}

void rtcReset() {
  memset(&rtc, 0, sizeof(rtc));
  rtc.hour = 99; rtc.minute = 99;   // mark the time as invalid
  rtc.pollMinutes = 0;
  rtc.contentDirty = 1;
  rtc.forceFull = 1;
  rtc.wxCode = 99;
  rtcSave();
}
