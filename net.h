// WiFi fast connect, HTTP/HTTPS JSON fetch, captive-portal detection and login, TLS session cache
#pragma once
#include <Arduino.h>
#include "sensors.h"

enum NetResult : uint8_t {
  NET_OK = 0, NET_NOT_MODIFIED, NET_WIFI_FAIL, NET_PORTAL, NET_PORTAL_LOGIN_FAIL,
  NET_HTTP_FAIL, NET_TLS_CERT, NET_TLS_MEM, NET_BAD_JSON
};

struct FetchInfo {
  NetResult res;
  int httpCode;
  uint32_t ms;
  char err[64];
};

bool netConnect();                 // fast connect, then a normal one; caches BSSID/channel/IP on success
void netOff();                     // turn WiFi off to save power
bool netIsConnected();
bool netSsidIsOpen(const char* ssid);  // scan: true if the SSID is an open (unencrypted) network; false if not found either
void netApplyPhy(uint8_t mode);        // 0 = 11g, 1 = 11n (rtc.phyMode)
uint8_t netLastDisconnectReason();     // reason code from the last WiFi disconnect event
// Fetch <serverUrl>/api/eink.json, writing the body into buf (NUL-terminated). sd may be nullptr.
// On failure with portal login enabled: probe over plain HTTP for interception, submit the login, fetch again
NetResult netFetchJson(char* buf, size_t cap, size_t& outLen, const char* rev, const SensorData* sd, FetchInfo& info);
// Fetch once with the given URL/TLS parameters (used by the provisioning page's "test connection"); same
// portal flow as above (reads cfg.portal*) and does not touch the global settings
NetResult netTestFetch(const char* url, uint8_t tlsMode, const uint8_t* fp, char* buf, size_t cap, FetchInfo& info);
const char* netResultName(NetResult r);
