#include "net.h"
#include "config.h"
#include "settings.h"
#include "rtc_mem.h"
#include "ca_certs.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "keys.h"

static_assert(sizeof(BearSSL::Session) <= sizeof(rtc.tlsSession), "tls session cache too small");

const char* netResultName(NetResult r) {
  static const char* N[] = {"ok", "not-modified", "wifi-fail", "portal", "portal-login-fail", "http-fail", "tls-cert", "tls-mem", "bad-json"};
  return N[r];
}

bool netIsConnected() { return WiFi.status() == WL_CONNECTED; }

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    keysPoll();
    delay(20);
  }
  return false;
}

static void cacheWifi() {
  memcpy(rtc.bssid, WiFi.BSSID(), 6);
  rtc.channel = WiFi.channel();
  rtc.ip = (uint32_t)WiFi.localIP(); rtc.gw = (uint32_t)WiFi.gatewayIP();
  rtc.mask = (uint32_t)WiFi.subnetMask(); rtc.dns = (uint32_t)WiFi.dnsIP();
  rtc.wifiCacheOk = 1;
}

static WiFiEventHandler g_evDisc, g_evConn;
static volatile uint8_t g_lastDisc = 0;
uint8_t netLastDisconnectReason() { return g_lastDisc; }
void netApplyPhy(uint8_t mode) { WiFi.setPhyMode(mode == 1 ? WIFI_PHY_MODE_11N : WIFI_PHY_MODE_11G); }
static void installWifiEvents() {
  static bool done = false; if (done) return; done = true;
  g_evDisc = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) { g_lastDisc = e.reason; LOG("wifi-ev: disconnected reason=%d (2=auth-expire 15=4way-timeout 201=no-ap 202=auth-fail 203=assoc-fail 204=hs-timeout)", e.reason); });
  g_evConn = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& e) { LOG("wifi-ev: associated ch%d bssid=%02X:%02X:%02X:%02X:%02X:%02X", e.channel, e.bssid[0], e.bssid[1], e.bssid[2], e.bssid[3], e.bssid[4], e.bssid[5]); });
}

bool netConnect() {
  if (!cfg.ssid[0]) return false;
  installWifiEvents();
  WiFi.persistent(false);
  WiFi.forceSleepWake(); delay(1);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  if (rtc.phyMode > 1) rtc.phyMode = 0;
  netApplyPhy(rtc.phyMode);   // 11g by default: in 11n mode some ASUS WiFi 6/7 routers kick the ESP8266 right after association (reason 8);
                              // enterprise APs may refuse 11g instead, hence the fallback below
  uint32_t t0 = millis();
  LOG("wifi: ssid='%s' pwlen=%d cache=%d phy=11%c", cfg.ssid, strlen(cfg.pass), rtc.wifiCacheOk, rtc.phyMode ? 'n' : 'g');
  if (rtc.wifiCacheOk) {
    WiFi.config(IPAddress(rtc.ip), IPAddress(rtc.gw), IPAddress(rtc.mask), IPAddress(rtc.dns));
    WiFi.begin(cfg.ssid, cfg.pass, rtc.channel, rtc.bssid, true);
    if (waitConnected(WIFI_FAST_TIMEOUT_MS)) { LOG("wifi: fast connect %lu ms", millis() - t0); rtc.wifiFail = 0; rtc.lastRssi = WiFi.RSSI(); return true; }
    LOG("wifi: fast connect failed, fallback");
    WiFi.disconnect(); delay(10);
    WiFi.config(0U, 0U, 0U);   // back to DHCP
    rtc.wifiCacheOk = 0;
  }
  WiFi.begin(cfg.ssid, cfg.pass);
  if (waitConnected(WIFI_SLOW_TIMEOUT_MS)) {
    LOG("wifi: connected %lu ms ip=%s ch=%d rssi=%d", millis() - t0, WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
    cacheWifi(); rtc.wifiFail = 0; rtc.lastRssi = WiFi.RSSI(); return true;
  }
  LOG("wifi: connect failed (status %d: 1=no-ssid 4=connect-fail 6=wrong-password)", WiFi.status());
  bool targetOpen = false, found = false;
  { int n = WiFi.scanNetworks(false, true);
    LOG("wifi: scan %d networks", n);
    for (int i = 0; i < n; i++) {
      if (i < 20) LOG("  '%s' ch%d %ddBm enc%d bssid=%s", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i), WiFi.BSSIDstr(i).c_str());
      if (WiFi.SSID(i) == cfg.ssid) { found = true; if (WiFi.encryptionType(i) == ENC_TYPE_NONE) targetOpen = true; }
    }
    WiFi.scanDelete(); }
  // An open network (portal-authenticated guest WiFi) configured with a password: the core sets authmode to WPA,
  // which filters out open APs and reports 201 no-ap. Retry without the password and, on success, store the
  // empty password so later connections go straight through
  if (targetOpen && cfg.pass[0]) {
    LOG("wifi: '%s' is an open network, retry without password", cfg.ssid);
    WiFi.disconnect(); delay(10);
    WiFi.begin(cfg.ssid);
    if (waitConnected(WIFI_SLOW_TIMEOUT_MS)) {
      LOG("wifi: connected (open) %lu ms ip=%s ch=%d rssi=%d", millis() - t0, WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
      cfg.pass[0] = 0; settingsSave();
      cacheWifi(); rtc.wifiFail = 0; rtc.lastRssi = WiFi.RSSI(); return true;
    }
    LOG("wifi: open retry failed (status %d)", WiFi.status());
  }
  // Visible in a scan but not connectable (typically association refused, reason 203: an enterprise AP rejecting
  // an old 11g device; or reason 8, an ASUS router kicking us in 11n) -> retry with the other PHY mode and
  // remember it if that works
  if (found) {
    uint8_t other = rtc.phyMode ? 0 : 1;
    LOG("wifi: retry with 11%c (last disconnect reason %d)", other ? 'n' : 'g', g_lastDisc);
    WiFi.disconnect(); delay(10);
    netApplyPhy(other);
    WiFi.begin(cfg.ssid, targetOpen ? nullptr : cfg.pass);
    if (waitConnected(WIFI_SLOW_TIMEOUT_MS)) {
      rtc.phyMode = other;
      if (targetOpen && cfg.pass[0]) { cfg.pass[0] = 0; settingsSave(); }
      LOG("wifi: connected (11%c) %lu ms ip=%s ch=%d rssi=%d", other ? 'n' : 'g', millis() - t0, WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
      cacheWifi(); rtc.wifiFail = 0; rtc.lastRssi = WiFi.RSSI(); return true;
    }
    LOG("wifi: 11%c retry failed (status %d, reason %d)", other ? 'n' : 'g', WiFi.status(), g_lastDisc);
    netApplyPhy(rtc.phyMode);
  }
  if (rtc.wifiFail < 250) rtc.wifiFail++;
  return false;
}

bool netSsidIsOpen(const char* ssid) {
  int n = WiFi.scanNetworks(false, true);
  bool open = false;
  for (int i = 0; i < n; i++) if (WiFi.SSID(i) == ssid && WiFi.encryptionType(i) == ENC_TYPE_NONE) open = true;
  WiFi.scanDelete();
  return open;
}

void netOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
}

// ---- URL parsing ----
struct Url { bool https; String host; uint16_t port; String path; };
static bool parseUrl(const char* s, Url& u) {
  String str(s);
  if (str.startsWith("https://")) { u.https = true; str.remove(0, 8); }
  else if (str.startsWith("http://")) { u.https = false; str.remove(0, 7); }
  else return false;
  int slash = str.indexOf('/');
  String hp = slash < 0 ? str : str.substring(0, slash);
  u.path = slash < 0 ? "/" : str.substring(slash);
  int colon = hp.indexOf(':');
  if (colon >= 0) { u.host = hp.substring(0, colon); u.port = hp.substring(colon + 1).toInt(); }
  else { u.host = hp; u.port = u.https ? 443 : 80; }
  if (u.path.endsWith("/")) u.path.remove(u.path.length() - 1);
  return u.host.length() > 0 && u.port > 0;
}

// ---- TLS client construction ----
static BearSSL::X509List* g_ca = nullptr;
static BearSSL::Session g_session;

static std::unique_ptr<WiFiClient> makeClient(const Url& u, uint8_t tlsMode, const uint8_t* fp, bool useSessionCache, FetchInfo& info, uint16_t rxBuf = 1024) {
  if (!u.https) return std::unique_ptr<WiFiClient>(new WiFiClient());
  uint32_t heap = ESP.getFreeHeap();
  if (heap < 16000) { info.res = NET_TLS_MEM; snprintf(info.err, sizeof(info.err), "heap %u", heap); return nullptr; }
  auto c = new BearSSL::WiFiClientSecure();
  c->setBufferSizes(rxBuf, 512);   // portal hosts rarely support MFLN, so the whole certificate chain arrives in one record: 4 KB needed
  if (tlsMode == TLS_FINGERPRINT) c->setFingerprint(fp);
  else if (tlsMode == TLS_CA) { if (!g_ca) g_ca = new BearSSL::X509List(ISRG_ROOT_X1); c->setTrustAnchors(g_ca); }
  else c->setInsecure();
  if (useSessionCache) {
    if (rtc.tlsSessionOk) memcpy((void*)&g_session, rtc.tlsSession, sizeof(BearSSL::Session));
    c->setSession(&g_session);
  }
  return std::unique_ptr<WiFiClient>(c);
}

static void classifyTlsError(WiFiClient* raw, FetchInfo& info) {
  auto c = static_cast<BearSSL::WiFiClientSecure*>(raw);
  char buf[48]; int e = c->getLastSSLError(buf, sizeof(buf));
  snprintf(info.err, sizeof(info.err), "ssl %d %s", e, buf);
  // BearSSL X509 error codes are 32..63; a fingerprint mismatch counts as a certificate error too
  if (e >= 32 && e <= 63) info.res = NET_TLS_CERT;
  else if (e == 0 && cfg.tlsMode != TLS_INSECURE) info.res = NET_TLS_CERT;
  else info.res = NET_HTTP_FAIL;
}

// ---- Core GET ----
static NetResult doGet(const Url& u, const String& fullPath, uint8_t tlsMode, const uint8_t* fp, bool sessionCache,
                       char* buf, size_t cap, size_t& outLen, FetchInfo& info, String& location) {
  outLen = 0; info.res = NET_HTTP_FAIL; info.err[0] = 0;
  auto client = makeClient(u, tlsMode, fp, sessionCache, info);
  if (!client) return info.res;
  if (u.https) system_update_cpu_freq(160);
  std::unique_ptr<HTTPClient> httpPtr(new HTTPClient());   // ~300 B, kept off the cont stack
  HTTPClient& http = *httpPtr;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  const char* hdrs[] = {"Location", "Content-Type", "X-Next-Wake"};
  http.collectHeaders(hdrs, 3);
  uint32_t t0 = millis();
  if (!http.begin(*client, u.host, u.port, fullPath, u.https)) { snprintf(info.err, sizeof(info.err), "begin fail"); return NET_HTTP_FAIL; }
  http.addHeader("User-Agent", "homelab-eink/" FW_VERSION);
  int code = http.GET();
  info.httpCode = code; info.ms = millis() - t0;
  if (u.https) system_update_cpu_freq(80);
  if (code <= 0) {
    if (u.https) classifyTlsError(client.get(), info);
    else { info.res = NET_HTTP_FAIL; snprintf(info.err, sizeof(info.err), "%s", http.errorToString(code).c_str()); }
    http.end();
    if (info.res == NET_TLS_CERT || info.res == NET_HTTP_FAIL) rtc.tlsSessionOk = 0;
    return info.res;
  }
  if (u.https && sessionCache) {
    memcpy(rtc.tlsSession, (const void*)&g_session, sizeof(BearSSL::Session));
    rtc.tlsSessionOk = 1;
  }
  String ctype = http.header("Content-Type");
  location = http.header("Location");
  if (code == 304) { http.end(); return info.res = NET_NOT_MODIFIED; }
  if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) { http.end(); return info.res = NET_PORTAL; }
  if (code == 200 && ctype.indexOf("json") < 0) { http.end(); snprintf(info.err, sizeof(info.err), "ctype %s", ctype.c_str()); return info.res = NET_PORTAL; }
  if (code != 200) { http.end(); snprintf(info.err, sizeof(info.err), "http %d", code); return info.res = NET_HTTP_FAIL; }
  // read the body (getString handles content-length / chunked)
  {
    String body = http.getString();
    outLen = min(body.length(), cap - 1);
    memcpy(buf, body.c_str(), outLen); buf[outLen] = 0;
  }
  http.end();
  if (outLen == 0) { snprintf(info.err, sizeof(info.err), "empty body"); return info.res = NET_HTTP_FAIL; }
  return info.res = NET_OK;
}

static String urlEncode(const char* s) {
  String o; char h[4];
  for (const char* p = s; *p; p++) {
    if (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') o += *p;
    else { snprintf(h, sizeof(h), "%%%02X", (unsigned char)*p); o += h; }
  }
  return o;
}

// ---- Portal probing ----
// Request over plain HTTP a URL that always returns 204 / a known body: a 3xx or any other page means a portal
// is intercepting us
enum ProbeResult : uint8_t { PROBE_OPEN, PROBE_PORTAL, PROBE_UNKNOWN };
struct ProbeDef { const char* url; int okCode; const char* okBody; };
static const ProbeDef PROBES[] = {
  { PORTAL_PROBE_URL,  204, nullptr },
  { PORTAL_PROBE_URL2, 200, "Success" },
};

// read the body prefix into out (not getString: portal pages are easily 20 KB and a String would eat the heap)
static size_t readBodyPrefix(HTTPClient& http, char* out, size_t cap, uint32_t timeoutMs = 3000) {
  WiFiClient* s = http.getStreamPtr(); size_t n = 0; uint32_t t0 = millis();
  if (!s) { out[0] = 0; return 0; }
  while (n < cap - 1 && millis() - t0 < timeoutMs) {
    int a = s->available();
    if (a > 0) { n += s->readBytes(out + n, min((size_t)a, cap - 1 - n)); t0 = millis(); }
    else if (!s->connected()) break;
    else delay(5);
  }
  out[n] = 0; return n;
}

static String originOf(const String& url) {   // "https://host:port"
  int p = url.indexOf("://"); if (p < 0) return "";
  int sl = url.indexOf('/', p + 3);
  return sl < 0 ? url : url.substring(0, sl);
}
static String pathOf(const String& url) {     // "/path?query"
  int p = url.indexOf("://"); if (p < 0) return url;
  int sl = url.indexOf('/', p + 3);
  return sl < 0 ? String("/") : url.substring(sl);
}
// prepend the hijacked request's host to a relative Location ("/login?x")
static String absLocation(const String& loc, const Url& req) {
  if (!loc.startsWith("/")) return loc;
  String o = req.https ? "https://" : "http://";
  o += req.host;
  if (req.port != (req.https ? 443 : 80)) { o += ':'; o += (unsigned)req.port; }
  o += loc;
  return o;
}

// When the portal answers with a 200 page and no Location, dig the redirect/form target out of the body:
// <meta http-equiv="refresh" content="0; url=...">, location.href= / location.replace(...), <form action=...>
static String extractRedirect(const char* body) {
  String s(body); String low(s); low.toLowerCase();
  int start = -1;
  int r = low.indexOf("refresh");
  if (r >= 0) { int u = low.indexOf("url=", r); if (u >= 0) start = u + 4; }
  if (start < 0) {
    int l = low.indexOf("location");
    while (l >= 0 && start < 0) {
      int eq = -1;
      for (int i = l + 8; i < (int)low.length() && i < l + 40; i++) { char ch = low[i]; if (ch == '=' || ch == '(') { eq = i + 1; break; } if (ch == ';' || ch == '\n') break; }
      if (eq > 0) { int q1 = low.indexOf('"', eq), q2 = low.indexOf('\'', eq); int q = (q1 < 0 || (q2 >= 0 && q2 < q1)) ? q2 : q1; if (q >= 0 && q - eq < 8) start = q + 1; }
      l = low.indexOf("location", l + 8);
    }
  }
  if (start < 0) { int a = low.indexOf("action="); if (a >= 0) start = a + 7; }
  if (start < 0) return "";
  while (start < (int)s.length() && (s[start] == ' ' || s[start] == '\'' || s[start] == '"')) start++;
  int end = start;
  while (end < (int)s.length()) { char ch = s[end]; if (ch == '\'' || ch == '"' || ch == '>' || ch == ' ' || ch == ';' || ch == ')' || ch == '\r' || ch == '\n' || ch == '\t') break; end++; }
  String url = s.substring(start, end);
  url.replace("&amp;", "&");
  if (!url.startsWith("http") && !url.startsWith("/")) return "";
  return url;
}

// body/cap: buffer for reading a 200 page's body (reuses the JSON buffer)
static ProbeResult portalProbe(String& location, char* body, size_t cap) {
  location = "";
  bool sawPortalPage = false;
  for (const ProbeDef& p : PROBES) {
    Url u; if (!parseUrl(p.url, u)) continue;
    WiFiClient client;
    std::unique_ptr<HTTPClient> httpPtr(new HTTPClient());   // ~300 B, kept off the cont stack
    HTTPClient& http = *httpPtr;
    http.setTimeout(PORTAL_PROBE_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(false);
    const char* hdrs[] = {"Location"};
    http.collectHeaders(hdrs, 1);
    if (!http.begin(client, u.host, u.port, u.path.length() ? u.path : "/", false)) continue;
    http.addHeader("User-Agent", "homelab-eink/" FW_VERSION);
    int code = http.GET();
    if (code <= 0) { LOG("portal probe: %s -> %s", u.host.c_str(), http.errorToString(code).c_str()); http.end(); continue; }
    String loc = absLocation(http.header("Location"), u);
    size_t n = 0; body[0] = 0;
    if (code == 200) { n = readBodyPrefix(http, body, cap); for (size_t i = 0; i < n; i++) if (body[i] == '\r' || body[i] == '\n' || body[i] == '\t') body[i] = ' '; }
    http.end();
    bool bodyOk = !p.okBody || (n && strstr(body, p.okBody));
    LOG("portal probe: %s -> %d loc=%s", u.host.c_str(), code, loc.c_str());
    if (code == p.okCode && bodyOk) return PROBE_OPEN;
    if (code >= 300 && code < 400) { location = loc; return PROBE_PORTAL; }   // a 302 is the clearest portal signal
    if (code == 200) {   // some other page came back: it is the portal page, so find the redirect target in the body
      sawPortalPage = true;
      LOG("portal probe: body %u B: %.200s", n, body);
      String url = absLocation(extractRedirect(body), u);
      if (url.length()) { LOG("portal probe: redirect in body -> %s", url.c_str()); location = url; return PROBE_PORTAL; }
    }
    // 5xx and friends: move on to the next probe URL
  }
  return sawPortalPage ? PROBE_PORTAL : PROBE_UNKNOWN;
}

// Follow the redirect chain to the real portal page. Aruba: probe URL returns 200 + a meta refresh -> same host
// with ?cmd=redirect&arubalp=... -> 302 to https://<portal>/upload/custom/.../login.html?cmd=login&mac=...
// GET only, no automatic following, at most 4 hops; stop at https or at a known portal path
static void resolvePortalUrl(String& loc, char* body, size_t cap) {
  for (int hop = 0; hop < 4 && loc.length(); hop++) {
    Url u; if (!parseUrl(loc.c_str(), u)) return;
    String path = pathOf(loc);
    if (u.https || path.startsWith("/cgi-bin/login") || path.startsWith("/auth/index.html") || path.startsWith("/upload/")) return;
    WiFiClient client;
    std::unique_ptr<HTTPClient> httpPtr(new HTTPClient());
    HTTPClient& http = *httpPtr;
    http.setTimeout(PORTAL_PROBE_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setReuse(false);
    const char* hdrs[] = {"Location"};
    http.collectHeaders(hdrs, 1);
    if (!http.begin(client, u.host, u.port, path, false)) return;
    http.addHeader("User-Agent", "Mozilla/5.0 homelab-eink/" FW_VERSION);
    int code = http.GET();
    String next;
    if (code >= 300 && code < 400) next = absLocation(http.header("Location"), u);
    else if (code == 200) {
      size_t n = readBodyPrefix(http, body, cap);
      for (size_t i = 0; i < n; i++) if (body[i] == '\r' || body[i] == '\n' || body[i] == '\t') body[i] = ' ';
      next = absLocation(extractRedirect(body), u);
    }
    http.end();
    LOG("portal: hop %d GET %s -> %d next=%s", hop, loc.c_str(), code, next.c_str());
    if (!next.length() || next == loc) return;
    loc = next;
  }
}

// ---- Portal login ----
struct PortalForm { String url; String fieldUser, fieldPass; String extra; };

// Login URL precedence: a configured full URL > a configured path (joined onto the portal host) > a preset for
// the portal type recognized from the redirect > the redirect target itself
static bool buildPortalForm(const String& location, PortalForm& f, FetchInfo& info) {
  String origin = originOf(location), path = pathOf(location);
  // Aruba controller built-in portal: 302 to /cgi-bin/login?cmd=login&..., the login page lives at
  // /auth/index.html or /upload/custom/..., and the form POSTs to /auth/index.html/u with fields
  // user / password / cmd=authenticate (the setup behind many corporate guest networks)
  bool aruba = path.startsWith("/cgi-bin/login") || path.startsWith("/auth/index.html") || path.startsWith("/upload/");
  String flds(cfg.portalFields); int c = flds.indexOf(',');
  f.fieldUser = c > 0 ? flds.substring(0, c) : "username"; f.fieldPass = c > 0 ? flds.substring(c + 1) : "password";
  f.fieldUser.trim(); f.fieldPass.trim();
  if (!f.fieldUser.length()) f.fieldUser = "username";
  if (!f.fieldPass.length()) f.fieldPass = "password";
  f.extra = cfg.portalExtra;
  if (cfg.portalUrl[0] == '/') {
    if (!origin.length()) { snprintf(info.err, sizeof(info.err), "no portal host for %s", cfg.portalUrl); return false; }
    f.url = origin + cfg.portalUrl;
  } else if (cfg.portalUrl[0]) {
    f.url = cfg.portalUrl;
  } else if (aruba && origin.length()) {
    f.url = origin + "/auth/index.html/u";
    if (f.fieldUser == "username") f.fieldUser = "user";
    if (f.extra.indexOf("cmd=") < 0) { if (f.extra.length()) f.extra += "&"; f.extra += "cmd=authenticate&Login=Log+In"; }
    LOG("portal: aruba preset");
  } else if (location.length()) {
    f.url = location;
  } else { snprintf(info.err, sizeof(info.err), "no login url"); return false; }
  return true;
}

static String urlDecode(const String& s) {
  String o; o.reserve(s.length());
  for (unsigned i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '+') o += ' ';
    else if (ch == '%' && i + 2 < s.length()) { char h[3] = {s[i + 1], s[i + 2], 0}; o += (char)strtoul(h, nullptr, 16); i += 2; }
    else o += ch;
  }
  return o;
}

// body doubles as the response buffer (used to decide whether the login succeeded); it is overwritten later by
// the re-fetched JSON
static bool portalLogin(const String& location, char* body, size_t bodyCap, FetchInfo& info) {
  PortalForm f; if (!buildPortalForm(location, f, info)) return false;
  Url u; if (!parseUrl(f.url.c_str(), u)) { snprintf(info.err, sizeof(info.err), "bad login url"); return false; }
  String path = pathOf(f.url); if (!path.length()) path = "/";
  String form = f.fieldUser + "=" + urlEncode(cfg.portalUser) + "&" + f.fieldPass + "=" + urlEncode(cfg.portalPass);
  if (f.extra.length()) { form += "&"; form += f.extra; }
  FetchInfo tmp = {};
  auto client = makeClient(u, TLS_INSECURE, nullptr, false, tmp, 6144);
  if (!client) { info.res = tmp.res; strcpy(info.err, tmp.err); return false; }
  LOG("portal login: POST %s://%s:%u%s  %s=%s %s=*** %s  heap=%u block=%u", u.https ? "https" : "http", u.host.c_str(), u.port, path.c_str(),
      f.fieldUser.c_str(), cfg.portalUser, f.fieldPass.c_str(), f.extra.c_str(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  if (u.https) system_update_cpu_freq(160);
  std::unique_ptr<HTTPClient> httpPtr(new HTTPClient());   // ~300 B, kept off the cont stack
  HTTPClient& http = *httpPtr;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);   // do not follow the 302: the Location carries the failure reason (errmsg=...)
  http.setReuse(false);
  const char* hdrs[] = {"Location"};
  http.collectHeaders(hdrs, 1);
  bool ok = false; int code = 0;
  if (!http.begin(*client, u.host, u.port, path, u.https)) { snprintf(info.err, sizeof(info.err), "login begin fail"); }
  else {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("User-Agent", "Mozilla/5.0 homelab-eink/" FW_VERSION);
    code = http.POST(form);
    String loc = code > 0 ? absLocation(http.header("Location"), u) : String();
    body[0] = 0;
    if (code > 0 && code != 302 && code != 303) {
      size_t n = readBodyPrefix(http, body, bodyCap);
      for (size_t i = 0; i < n; i++) if (body[i] == '\r' || body[i] == '\n' || body[i] == '\t') body[i] = ' ';
      LOG("portal login: body %u B: %.200s", n, body);
    }
    http.end();
    int em = loc.indexOf("errmsg=");
    if (code <= 0) {
      if (u.https) { classifyTlsError(client.get(), info); } else snprintf(info.err, sizeof(info.err), "login: %s", http.errorToString(code).c_str());
    } else if (em >= 0) {
      String m = loc.substring(em + 7); int amp = m.indexOf('&'); if (amp >= 0) m.remove(amp);
      snprintf(info.err, sizeof(info.err), "portal: %s", urlDecode(m).c_str());
    } else if (code >= 400) {
      snprintf(info.err, sizeof(info.err), "login http %d", code);
    } else if (strstr(body, "Authentication failed")) {   // failure text when Aruba answers with a 200 page instead
      snprintf(info.err, sizeof(info.err), "portal: Authentication failed");
    } else if (cfg.portalOk[0] && !strstr(body, cfg.portalOk)) {
      snprintf(info.err, sizeof(info.err), "login http %d, no '%s'", code, cfg.portalOk);
    } else ok = true;
    if (loc.length()) LOG("portal login: -> %s", loc.c_str());
  }
  if (u.https) system_update_cpu_freq(80);
  LOG("portal login: code=%d ok=%d %s", code, ok, ok ? "" : info.err);
  return ok;
}

// Main request + portal detection / login / retry
static NetResult fetchWithPortal(const Url& u, const String& path, uint8_t tlsMode, const uint8_t* fp, bool sessionCache,
                                 char* buf, size_t cap, size_t& outLen, FetchInfo& info) {
  String loc;
  NetResult r = doGet(u, path, tlsMode, fp, sessionCache, buf, cap, outLen, info, loc);
  if (r == NET_OK || r == NET_NOT_MODIFIED) return r;
  if (r == NET_PORTAL) loc = absLocation(loc, u);
  else {
    // Direct connection failed (timeout / cert error / refused): the portal may only be hijacking 80/443, so probe
    // to confirm; with portal login disabled, do not send the extra requests
    if (!cfg.portalEnable) return r;
    static FetchInfo first; first = info;   // the cont stack is only 4 KB, so keep everything possible off it along this path
    if (portalProbe(loc, buf, cap) != PROBE_PORTAL) { info = first; return r; }
    r = info.res = NET_PORTAL; snprintf(info.err, sizeof(info.err), "portal (probe, direct %s)", netResultName(first.res));
  }
  LOG("net: portal intercept (code %d, loc=%s)", info.httpCode, loc.c_str());
  if (!cfg.portalEnable) return r;
  resolvePortalUrl(loc, buf, cap);
  if (!portalLogin(loc, buf, cap, info)) { if (rtc.portalFail < 250) rtc.portalFail++; return info.res = NET_PORTAL_LOGIN_FAIL; }
  delay(300);   // it takes a moment for the allow rule to take effect
  String loc2;
  if (portalProbe(loc2, buf, cap) == PROBE_PORTAL) {
    if (rtc.portalFail < 250) rtc.portalFail++;
    snprintf(info.err, sizeof(info.err), "still behind portal after login");
    return info.res = NET_PORTAL_LOGIN_FAIL;
  }
  rtc.portalFail = 0;
  LOG("portal: login ok, retry fetch");
  r = doGet(u, path, tlsMode, fp, sessionCache, buf, cap, outLen, info, loc);
  if (r == NET_PORTAL) return info.res = NET_PORTAL_LOGIN_FAIL;
  return r;
}

NetResult netFetchJson(char* buf, size_t cap, size_t& outLen, const char* rev, const SensorData* sd, FetchInfo& info) {
  memset(&info, 0, sizeof(info));
  static Url u; if (!parseUrl(cfg.serverUrl, u)) { snprintf(info.err, sizeof(info.err), "bad url"); return info.res = NET_HTTP_FAIL; }
  String path = u.path + "/api/eink.json?rev=" + urlEncode(rev ? rev : "") + "&fw=" FW_VERSION;
  if (sd) {
    char q[64]; snprintf(q, sizeof(q), "&vbat=%.2f&chg=%d", sd->vbat, sd->charging); path += q;
    if (sd->shtOk) { snprintf(q, sizeof(q), "&t=%.1f&h=%.1f", sd->temp, sd->humi); path += q; }
  }
  return fetchWithPortal(u, path, cfg.tlsMode, cfg.fingerprint, true, buf, cap, outLen, info);
}

NetResult netTestFetch(const char* url, uint8_t tlsMode, const uint8_t* fp, char* buf, size_t cap, FetchInfo& info) {
  memset(&info, 0, sizeof(info));
  Url u; if (!parseUrl(url, u)) { snprintf(info.err, sizeof(info.err), "bad url"); return info.res = NET_HTTP_FAIL; }
  size_t n = 0;
  return fetchWithPortal(u, u.path + "/api/eink.json?test=1", tlsMode, fp, false, buf, cap, n, info);
}
