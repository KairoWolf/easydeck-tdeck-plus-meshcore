#pragma once
#include <functional>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "core/Model.h"
#include <math.h>
#include <string>
#include <cstdio>

class SimModel : public Model {
public:
  SimModel();
  std::function<void(const LogRecord&)> onAppend, onUpdate;
  void incoming(int contact, const char* text);
  void deliverPending();
  void advanceTime(uint32_t s) { _now += s; }

  int  listConversations(ConvSummary* out, int max) override;
  int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) override;
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
  int channelScope(const uint8_t* key, char* out) override { snprintf(out, 32, "%s", _chscope); return _chmode; }
  void advertise(bool far) override { _tx++; }
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
