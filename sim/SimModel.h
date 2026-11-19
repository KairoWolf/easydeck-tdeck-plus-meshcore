#pragma once
#include <functional>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "core/Model.h"
#include <lvgl.h>
#include "ui/MapView.h"
static inline uint32_t simMillis() { return lv_tick_get(); }
static inline uint32_t simTilesInArea(double a, double b, double r, int z0, int z1) { return mapv::tilesInArea(a, b, r, z0, z1); }
#include <math.h>
#include <string>
#include <cstdio>

class SimModel : public Model {
public:
  SimModel();
  std::function<void(const LogRecord&)> onAppend, onUpdate;
  void incoming(int contact, const char* text);
  void incomingChannel(int ch, const char* sender, const char* text);
  void deliverPending();
  void advanceTime(uint32_t s) { _now += s; }

  int  listConversations(ConvSummary* out, int max) override;
  int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) override;
  int  searchMessages(const char* q, uint32_t* seqs, int max) override;
  bool getMessage(uint32_t seq, LogRecord& out) override;
  int  unreadCount(const ConvKey& k) override;
  int  totalUnread() override;
  void markRead(const ConvKey& k) override;
  bool sendText(const ConvKey& k, const char* text) override;
  int  maxTextLen(const ConvKey& k) override { return k.kind == KIND_CHANNEL ? 150 - (int)strlen(_name) - 2 : 150; }

  int  contactCount() override;
  bool contactAt(int i, ContactView& out) override;
  bool findContact(const uint8_t* key, ContactView& out) override;
  void setFavorite(const uint8_t* key, bool fav) override;
  void setShareLocation(const uint8_t* key, bool share) override;
  bool removeContact(const uint8_t* key) override;
  bool requestLocation(const uint8_t* key) override { return true; }
  void noteLocation(const uint8_t* key, double lat, double lon) override {
    for (auto& c : _contacts) if (!memcmp(c.key, key, LOG_KEY_LEN)) { c.has_location = true; c.lat = lat; c.lon = lon; c.location_time = _now; }
  }
  bool resetRoute(const uint8_t* key) override { return true; }
  bool shareContact(const uint8_t* key) override { return true; }
  int  channelCount() override;
  bool channelAt(int i, ChannelView& out) override;
  bool findChannel(const uint8_t* key, ChannelView& out) override;
  bool joinChannel(const char* name) override;
  bool leaveChannel(const uint8_t* key) override;
  bool createPrivateChannel(const char* name, char* hex) override { snprintf(hex, 33, "3fa94c0d17b2e85a6c41d9f02be7743c"); return joinChannel(name); }
  bool joinPrivateChannel(const char* name, const char* hex) override { return strlen(hex) == 32 && joinChannel(name); }
  bool channelSecretHex(const uint8_t* key, char* hex) override { snprintf(hex, 33, "3fa94c0d17b2e85a6c41d9f02be7743c"); return true; }
  int _chmode = 0; char _chscope[32] = "";
  void setChannelScope(const uint8_t* key, int mode, const char* name) override { _chmode = mode; snprintf(_chscope, 32, "%s", name ? name : ""); }
  std::vector<std::pair<std::string, std::string>> _notes;
  void contactNote(const uint8_t* key, char* out) override {
    out[0] = 0; std::string k((const char*)key, LOG_KEY_LEN);
    for (auto& n : _notes) if (n.first == k) { snprintf(out, CONTACT_NOTE_MAX + 1, "%s", n.second.c_str()); return; }
  }
  bool setContactNote(const uint8_t* key, const char* note) override {
    std::string k((const char*)key, LOG_KEY_LEN);
    for (auto& n : _notes) if (n.first == k) { n.second = note ? note : ""; return true; }
    _notes.push_back({ k, note ? note : "" });
    return true;
  }
  std::vector<std::string> _muted;
  bool channelMuted(const uint8_t* key) override { std::string k((const char*)key, LOG_KEY_LEN); for (auto& x : _muted) if (x == k) return true; return false; }
  bool setChannelMuted(const uint8_t* key, bool mu) override {
    std::string k((const char*)key, LOG_KEY_LEN);
    for (size_t i = 0; i < _muted.size(); i++) if (_muted[i] == k) { if (!mu) _muted.erase(_muted.begin() + i); return true; }
    if (mu) _muted.push_back(k);
    return true;
  }
  int channelScope(const uint8_t* key, char* out) override { snprintf(out, 32, "%s", _chscope); return _chmode; }
  void advertise(bool far) override { _tx++; if (far) _adv_ms = simMillis(); }
  uint32_t _adv_ms = 0;
  void advertEcho(bool& active, uint32_t& age_s, int& count) override {   // pretend 2 repeaters re-send it
    active = _adv_ms != 0; age_s = active ? (simMillis() - _adv_ms) / 1000 : 0;
    count = !active ? 0 : age_s >= 3 ? 2 : age_s >= 1 ? 1 : 0;
  }
  RadioStats radioStats() override { RadioStats r = { 1342, _tx, 3, -96.5f, 7.25f, -118, 5400 }; return r; }
  int  recentlyHeard(HeardView* out, int max) override;
  AdminState _adm = ADM_NONE;
  std::vector<CliLine> _cli;
  bool adminLogin(const uint8_t* key, const char* pw) override { _adm = ADM_ADMIN; return true; }
  AdminState adminState(const uint8_t* key) override { return _adm; }
  bool requestStatus(const uint8_t* key) override { return true; }
  bool getStatus(const uint8_t* key, RepeaterStatus& s) override {
    if (_adm == ADM_NONE) return false;
    s = { true, _now - 20, 4105, 0, -116, -98, 6.25f, 48211, 20114, 3920, 1209600, 9022, 11092, 30111, 18100, 2, 140, 3301 };
    return true;
  }
  bool sendCli(const uint8_t* key, const char* cmd) override {
    CliLine a = { true, "" }; snprintf(a.text, sizeof(a.text), "%s", cmd); _cli.push_back(a);
    CliLine b = { false, "" };
    if (!strcmp(cmd, "ver")) snprintf(b.text, sizeof(b.text), "v1.18.0 (Build: 15-Oct-2026)");
    else if (!strcmp(cmd, "clock")) snprintf(b.text, sizeof(b.text), "06:46 - 19/11/2026 UTC");
    else snprintf(b.text, sizeof(b.text), "OK");
    _cli.push_back(b);
    return true;
  }
  int cliLines(const uint8_t* key, CliLine* out, int max) override {
    int n = 0; for (auto& l : _cli) { if (n >= max) break; out[n++] = l; } return n;
  }
  bool discover() override { return true; }
  int  discovered(DiscoveredView* out, int max) override {
    const char* names[] = { "Taree Hill Rptr", "", "Forster RPT" };
    const float st[] = { 6.5f, -3.25f, 1.0f }, su[] = { 7.75f, -5.5f, 0.5f };
    int n = 0;
    for (int i = 0; i < 3 && n < max; i++) {
      DiscoveredView& v = out[n++];
      memset(&v, 0, sizeof(v));
      for (int k = 0; k < LOG_KEY_LEN; k++) v.key[k] = (uint8_t)(0x40 + i * 17 + k);
      snprintf(v.name, sizeof(v.name), "%s", names[i]);
      v.type = 2; v.snr_them = st[i]; v.snr_us = su[i];
    }
    return n;
  }
  int  packetLog(PacketView* out, int max) override {
    static const PacketView demo[] = {
      { 3, 7.25f, -96, 112, 4, 1, 2, 0 }, { 9, 9.5f, -88, 54, 3, 0, 0, 0 }, { 14, -4.0f, -117, 131, 5, 1, 3, 1 },
      { 41, 6.0f, -101, 98, 2, 0, 1, 0 }, { 75, 2.25f, -109, 112, 4, 1, 4, 0 }, { 130, 11.0f, -79, 140, 5, 1, 1, 1 } };
    int n = 0;
    for (auto& p : demo) { if (n >= max) break; out[n++] = p; }
    return n;
  }

  const char* nodeName() override { return _name; }
  void setNodeName(const char* n) override { snprintf(_name, sizeof(_name), "%s", n); }
  void nodeId(char* out, int n) override;
  RadioSettings radio() override;
  void setRadio(const RadioPreset& p) override;
  const char* regionScope() override { return _scope; }
  void setRegionScope(const char* s) override { snprintf(_scope, sizeof(_scope), "%s", s); }
  int  tzOffset() override { return _tz; }
  void setTzOffset(int h) override { _tz = h; }
  void setTime(uint32_t t) override { _now = t; }
  uint32_t blePin() override { return 123456; }
  void setBluetooth(bool on) override { _bt = on; }
  void setGps(bool on) override { _gps = on; }
  bool advertLocation() override { return _advloc; }
  void setAdvertLocation(bool on) override { _advloc = on; }
  DeviceStatus status() override;
  UiSettings& ui() override { return _ui; }
  void saveUi() override {}

  void setBacklight(uint8_t level) override {}
  void setKeyboardLight(bool on) override {}
  void playSound(SoundKind k) override {}
  void notifyFlash() override {}
  void rebootToLauncher() override {}
  void factoryReset() override {}
  const char* diagnostics() override { return "Simulator"; }
  std::string _dir = SIM_TILE_DIR "/..";
  bool readTextFile(const char* p, char* buf, int max) override {
    FILE* f = fopen((_dir + p).c_str(), "rb"); if (!f) return false;
    int n = (int)fread(buf, 1, max - 1, f); buf[n] = 0; fclose(f); return true;
  }
  bool writeTextFile(const char* p, const char* t) override {
    FILE* f = fopen((_dir + p).c_str(), "wb"); if (!f) return false; fputs(t, f); fclose(f); return true;
  }
  bool renameFile(const char* a, const char* b) override { return ::rename((_dir + a).c_str(), (_dir + b).c_str()) == 0; }
  int selfTest(SelfTestItem* out, int max) override {
    const SelfTestItem demo[] = { { "Radio", TEST_PASS, "SX1262 ok, RX 1342 TX 57" }, { "Keyboard", TEST_PASS, "found" },
      { "Touch", TEST_PASS, "GT911 found" }, { "SD card", TEST_PASS, "mounted" }, { "GPS", TEST_WARN, "searching, 3 sats" },
      { "Battery", TEST_PASS, "3.99 V (82%)" }, { "Sound", TEST_PASS, "I2S ready" }, { "Bluetooth", TEST_PASS, "advertising" },
      { "Memory", TEST_PASS, "PSRAM 7400 KB free" }, { "Clock", TEST_PASS, "set" } };
    int n = 0; for (auto& d : demo) { if (n >= max) break; out[n++] = d; } return n;
  }
  const char* lastResetReason() override { return ""; }
  bool crashedLastBoot() override { return false; }
  uint32_t inputEdges(int dir) override { return 0; }
  const char* tileRoot() override { return "S:" SIM_TILE_DIR; }
  int contactPath(const uint8_t* key, PathHop* out, int max) override {   // fake: everyone 2 hops away
    if (max < 2) return 0;
    PathHop a = {}; a.hash[0] = 0x8C; a.hash_len = 1; snprintf(a.name, sizeof(a.name), "Taree Hill Rptr"); a.matches = 1;
    PathHop b = {}; b.hash[0] = 0xD6; b.hash_len = 1; snprintf(b.name, sizeof(b.name), "Forster RPT"); b.matches = 2;
    out[0] = a; out[1] = b;
    return 2;
  }
  OwnerInfo _owner = {};
  void ownerInfo(OwnerInfo& o) override { o = _owner; }
  void setOwnerInfo(const OwnerInfo& in) override { _owner = in; }
  void gpsDetail(GpsDetail& g) override {
    g = GpsDetail();
    g.enabled = _gps;
    if (!_gps) return;
    g.valid = true; g.lat = -31.9110; g.lon = 152.4630; g.alt_m = 23; g.speed_kmh = 4.6f; g.course_deg = 247; g.hdop = 0.9f;
    g.sats_used = 8; g.fix_type = 3; g.fix_age_s = 0; g.sentences = 1234;
    static const GpsSat fake[] = { {4,45,38,120,'G'}, {5,30,25,200,'G'}, {9,70,41,10,'G'}, {12,10,0,330,'G'}, {17,5,12,80,'G'},
      {19,60,33,250,'G'}, {23,20,29,45,'G'}, {7,50,30,100,'C'}, {10,15,18,280,'C'}, {29,35,36,160,'C'} };
    g.n = sizeof(fake) / sizeof(fake[0]);
    for (int i = 0; i < g.n; i++) g.sats[i] = fake[i];
    g.sats_view = g.n;
  }
  std::vector<Waypoint> _wps = { { "Home", -31.9105, 152.4620, 0 }, { "Car park", -31.9168, 152.4711, 0 } };
  int waypointCount() override { return (int)_wps.size(); }
  bool waypointAt(int i, Waypoint& o) override { if (i < 0 || i >= (int)_wps.size()) return false; o = _wps[i]; return true; }
  bool addWaypoint(const Waypoint& w) override { if (_wps.size() >= MAX_WAYPOINTS) return false; _wps.push_back(w); return true; }
  void removeWaypoint(int i) override { if (i >= 0 && i < (int)_wps.size()) _wps.erase(_wps.begin() + i); }
  // fake Wi-Fi / map download for UI testing
  uint32_t _scan_ms = 0; bool _scanning = false;
  char _ssid[33] = ""; char _url[192] = "";
  TileDlStatus _dl; uint32_t _dl_ms = 0;
  bool wifiScanStart() override { _scanning = true; _scan_ms = simMillis(); return true; }
  void wifiScanStop() override { _scanning = false; }
  int wifiScanResults(WifiNet* out, int max) override {
    if (!_scanning) return 0;
    if (simMillis() - _scan_ms < 1500) return -1;
    _scanning = false;
    static const WifiNet nets[] = { { "HomeNet-5G", -48, true }, { "Telstra4F2A", -61, true }, { "Library Free WiFi", -77, false } };
    int n = max < 3 ? max : 3;
    for (int i = 0; i < n; i++) out[i] = nets[i];
    return n;
  }
  void setWifi(const char* ssid, const char* pass) override { snprintf(_ssid, sizeof(_ssid), "%s", ssid ? ssid : ""); }
  const char* wifiSsid() override { return _ssid; }
  void setTileUrl(const char* url) override { snprintf(_url, sizeof(_url), "%s", url ? url : ""); }
  const char* tileUrl() override { return _url; }
  bool hasSd() override { return true; }
  bool tileDownloadStart(double lat, double lon, double r, int zmin, int zmax) override {
    _dl = TileDlStatus();
    if (!_ssid[0]) { _dl.state = DL_FAILED; snprintf(_dl.msg, sizeof(_dl.msg), "Choose a Wi-Fi network first"); return false; }
    _dl.state = DL_CONNECTING; _dl.total = simTilesInArea(lat, lon, r, zmin, zmax); _dl_ms = simMillis();
    snprintf(_dl.msg, sizeof(_dl.msg), "Connecting to %s...", _ssid);
    return true;
  }
  void tileDownloadCancel() override { if (_dl.state == DL_RUNNING || _dl.state == DL_CONNECTING) { _dl.state = DL_CANCELLED; snprintf(_dl.msg, sizeof(_dl.msg), "Download cancelled"); } }
  TileDlStatus tileDownloadStatus() override {
    if (_dl.state == DL_CONNECTING && simMillis() - _dl_ms > 800) { _dl.state = DL_RUNNING; snprintf(_dl.msg, sizeof(_dl.msg), "Downloading..."); }
    if (_dl.state == DL_RUNNING) {
      uint32_t d = (simMillis() - _dl_ms - 800) / 4;
      _dl.done = d > _dl.total ? _dl.total : d;
      _dl.skipped = _dl.done / 5; _dl.fetched = _dl.done - _dl.skipped; _dl.bytes = _dl.fetched * 14000;
      if (_dl.done >= _dl.total) { _dl.state = DL_DONE; snprintf(_dl.msg, sizeof(_dl.msg), "Done: %lu new, %lu already there", (unsigned long)_dl.fetched, (unsigned long)_dl.skipped); }
    }
    return _dl;
  }
  std::vector<TrackPoint> _track;
  void trackAppend(const TrackPoint& p) override { _track.push_back(p); }
  int  trackLoad(TrackPoint* out, int max) override {
    if (_track.empty()) {   // demo trail walking out of Taree
      for (int i = 0; i < 40; i++) _track.push_back({ (float)(-31.9110 - i * 0.0009 + 0.0004 * sin(i * 0.5)), (float)(152.4630 + i * 0.0011), _now - (40 - i) * 60 });
    }
    int n = 0;
    for (auto& p : _track) { if (n >= max) break; out[n++] = p; }
    return n;
  }
  void trackClear() override { _track.clear(); }

private:
  void append(LogRecord& r, bool notify);
  void update(const LogRecord& r);
  std::vector<ContactView> _contacts;
  std::vector<ChannelView> _channels;
  std::vector<LogRecord> _log;
  char _name[32] = "Alex";
  char _scope[32] = "au-nsw-tro";
  int _tz = 11;
  bool _bt = true, _gps = true, _advloc = false;
  uint32_t _now;
  int _pending_deliver = -1;
  uint32_t _tx = 57;
  RadioSettings _radio = { 915.075f, 125.0f, 9, 5, 22 };
  UiSettings _ui;
};
