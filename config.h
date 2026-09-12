// Hardware and compile-time configuration -- 4.2" board revB-230621 + HINK-E04A13-A0 (Z96)
#pragma once
#include <Arduino.h>

#define FW_VERSION        "hd-0.1.1"

// ---- Display (SPI) ----
#define EPD_CS   15
#define EPD_DC   0
#define EPD_RST  2
#define EPD_BUSY 4
#define EPD_ROTATION 0        // 0 or 2; use 2 if the case orientation is wrong (also changeable on the provisioning page)

// ---- I2C: SHT30 + BL8025T ----
#define I2C_SDA  13
#define I2C_SCL  14
#define SHT30_ADDR 0x44

// ---- Battery ----
#define BAT_SWITCH_PIN 12      // high enables the battery divider and powers the SHT30
#define BAT_ADC_PIN    A0
#define BAT_ADC_COEF   (0.0009765625f * 5.607f)   // same as the stock firmware
#define BAT_LOW_V      3.30f   // low-battery notice, then sleep forever
#define BAT_DEAD_V     3.10f   // sleep immediately without drawing
#define BAT_CHG_FULL_V 4.32f   // median of trusted samples above this means charging (measured: 4.34-4.35 charging, 4.28 unplugged; ADC only sampled with RF on)
#define BAT_CHG_RISE_V 0.06f   // a rise above this across the last 5 trusted samples means charging (CC stage before full)

// ---- Keys (active low) ----
// SW1 = EN reset; SW2 and the side key next to USB = GPIO0; SW3 = GPIO3 (RX)
#define KEY2_PIN 0
#define KEY3_PIN 3
#define KEY_DEBOUNCE_MS 30
#define KEY_LONG_MS     900
#define WAKE_KEY_WINDOW_MS   3000   // after a manual reset, wait this long for a key to enter the interactive window
#define INTERACT_WINDOW_MS  20000   // idle timeout of the interactive window

// ---- Provisioning hotspot (same as the stock firmware) ----
#define AP_SSID   "ESP8266 E-Paper"
#define AP_PASS   "333333333"
#define AP_IP     IPAddress(192, 168, 3, 3)
#define PORTAL_TIMEOUT_MS (10UL * 60UL * 1000UL)

// ---- Refresh policy ----
#define FULL_REFRESH_EVERY_MIN 60     // full refresh every N minutes (on the hour)
#define WIFI_FAST_TIMEOUT_MS   6000
#define WIFI_SLOW_TIMEOUT_MS   15000
#define HTTP_TIMEOUT_MS        8000
#define JSON_BUF_SIZE          4096
#define NTP_HOUR               3      // hour of the day at which to also run an NTP sync
#define NTP_TZ_SECONDS         (8 * 3600)

// ---- Upstream WiFi captive-portal detection ----
// Portals usually only hijack 80/443: a direct connection to a port like 8090 just times out and HTTPS just
// fails certificate validation, so no 302 is ever seen. When the main request fails and portal login is
// enabled, probe a plain-HTTP URL that always returns 204 to confirm whether we are being intercepted.
#define PORTAL_PROBE_URL        "http://connect.rom.miui.com/generate_204"      // expects 204 (reachable inside and outside China)
#define PORTAL_PROBE_URL2       "http://captive.apple.com/hotspot-detect.html"  // fallback, expects 200 + "Success"
#define PORTAL_PROBE_TIMEOUT_MS 5000

// ---- Serial debug: TX only, RX (GPIO3) is reserved for key 3 ----
#define DBG_BAUD 115200
#define LOG(...) do { Serial.printf(__VA_ARGS__); Serial.print('\n'); } while (0)
