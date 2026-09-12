#include "keys.h"
#include "config.h"
// Key 2 (GPIO0) shares the display DC pin: after display init it is an output, so switch it back to input
// briefly while reading the key, then restore it as a high output. (GxEPD2 re-drives DC on every command, so
// restoring it high is safe; only call this while the display is not transferring data.)
static int readKey2() {
  pinMode(KEY2_PIN, INPUT_PULLUP);
  delayMicroseconds(80);
  int v = digitalRead(KEY2_PIN);
  pinMode(KEY2_PIN, OUTPUT); digitalWrite(KEY2_PIN, HIGH);
  return v;
}
static inline int readKey(uint8_t pin) { return pin == KEY2_PIN ? readKey2() : digitalRead(pin); }
static uint8_t g_pending = 0;
void keysPoll() {
  if (g_pending) return;
  if (readKey2() == LOW) { delay(KEY_DEBOUNCE_MS); if (readKey2() == LOW) { g_pending = 2; LOG("keysPoll: key 2 latched"); } }
  else if (digitalRead(KEY3_PIN) == LOW) { delay(KEY_DEBOUNCE_MS); if (digitalRead(KEY3_PIN) == LOW) { g_pending = 3; LOG("keysPoll: key 3 latched"); } }
}
uint8_t keysPending() { uint8_t k = g_pending; g_pending = 0; return k; }
void keysInit() { pinMode(KEY2_PIN, INPUT_PULLUP); pinMode(KEY3_PIN, INPUT_PULLUP); }
bool keyDown(uint8_t pin) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 3; i++) { if (readKey(pin) == LOW) n++; delay(KEY_DEBOUNCE_MS / 3); }
  return n >= 2;
}
uint8_t keysWait(uint32_t ms, bool& longPress) {
  longPress = false;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    uint8_t k = 0;
    if (readKey2() == LOW) k = 2; else if (digitalRead(KEY3_PIN) == LOW) k = 3;
    if (k) {
      uint8_t pin = k == 2 ? KEY2_PIN : KEY3_PIN;
      delay(KEY_DEBOUNCE_MS);
      if (readKey(pin) != LOW) continue;
      uint32_t p0 = millis();
      while (readKey(pin) == LOW && millis() - p0 < 3000) delay(5);
      longPress = millis() - p0 >= KEY_LONG_MS;
      delay(KEY_DEBOUNCE_MS);
      LOG("keysWait: key %d held %lu ms", k, millis() - p0);
      return k;
    }
    delay(5);
  }
  return 0;
}
