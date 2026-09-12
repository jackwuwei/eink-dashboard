#pragma once
#include <Arduino.h>
void    keysInit();
bool    keyDown(uint8_t pin);                 // debounced instantaneous state
// Wait up to `ms` for a key; returns 0 (timeout) / 2 / 3. longPress means held for >= KEY_LONG_MS
uint8_t keysWait(uint32_t ms, bool& longPress);
void    keysPoll();                           // call inside WiFi/refresh wait loops: remembers pressed keys
uint8_t keysPending();                        // take and clear the remembered key (0 = none)
