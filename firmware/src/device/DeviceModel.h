#pragma once
// DeviceModel — the real implementation of the UI's Model on top of MeshCore
// (EasyMesh), the MessageLog, and the T-Deck Plus hardware.
#include "core/Model.h"

class MessageLog;
class MultiSerialInterface;
class BaseSerialInterface;
class DataStore;

class DeviceModel : public Model {
public:
  void begin(MessageLog* log, MultiSerialInterface* serial, DataStore* store, BaseSerialInterface* ble);
  void noteLocation(const uint8_t* pubkey, double lat, double lon) override;   // telemetry reply / LOC / SOS message

  int  listConversations(ConvSummary* out, int max) override;
  int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) override;
  bool getMessage(uint32_t seq, LogRecord& out) override;
  int  unreadCount(const ConvKey& k) override;
  int  totalUnread() override;
  void markRead(const ConvKey& k) override;
  bool sendText(const ConvKey& k, const char* text) override;
  int  maxTextLen(const ConvKey& k) override;

  int  contactCount() override;
  bool contactAt(int i, ContactView& out) override;
  bool findContact(const uint8_t* key, ContactView& out) override;
  void setFavorite(const uint8_t* key, bool fav) override;
  void setShareLocation(const uint8_t* key, bool share) override;
  bool removeContact(const uint8_t* key) override;
  bool requestLocation(const uint8_t* key) override;
  bool resetRoute(const uint8_t* key) override;
  bool shareContact(const uint8_t* key) override;
  int  channelCount() override;
  bool channelAt(int i, ChannelView& out) override;
  bool findChannel(const uint8_t* key, ChannelView& out) override;
  bool joinChannel(const char* name) override;
  bool leaveChannel(const uint8_t* key) override;
  bool createPrivateChannel(const char* name, char* hex_out33) override;
  bool joinPrivateChannel(const char* name, const char* hex32) override;
  bool channelSecretHex(const uint8_t* key, char* hex_out33) override;
  void setChannelScope(const uint8_t* key, int mode, const char* name) override { easy_mesh_setChannelScope(key, mode, name); }
  int  channelScope(const uint8_t* key, char* name_out32) override { return easy_mesh_channelScope(key, name_out32); }
  static void easy_mesh_setChannelScope(const uint8_t* key, int mode, const char* name);
  static int  easy_mesh_channelScope(const uint8_t* key, char* out);
  void advertise(bool far) override;
  RadioStats radioStats() override;
  int  recentlyHeard(HeardView* out, int max) override;
  int  packetLog(PacketView* out, int max) override;
  bool adminLogin(const uint8_t* key, const char* password) override;
  AdminState adminState(const uint8_t* key) override;
  bool requestStatus(const uint8_t* key) override;
  bool getStatus(const uint8_t* key, RepeaterStatus& out) override;
  bool sendCli(const uint8_t* key, const char* cmd) override;
  int  cliLines(const uint8_t* key, CliLine* out, int max) override;
  bool discover() override;
  // called by DeviceUI with mesh responses / CLI replies
  bool onResponse(const uint8_t* pubkey, uint32_t tag, const uint8_t* data, uint8_t len);
  void onCliReply(const uint8_t* pubkey, const char* text);
  void loopRooms();   // re-join remembered rooms after boot
  bool clockTrusted();
  bool _clock_trusted = false;
  uint32_t _clk_ref_rtc = 0, _clk_ref_ms = 0;
  int  discovered(DiscoveredView* out, int max) override;

  const char* nodeName() override;
  void setNodeName(const char* name) override;
  void nodeId(char* out, int n) override;
  RadioSettings radio() override;
  void setRadio(const RadioPreset& p) override;
  const char* regionScope() override;
  void setRegionScope(const char* scope) override;
  int  tzOffset() override;
  void setTzOffset(int hours) override;
  void setTime(uint32_t unix_utc) override;
  uint32_t blePin() override;
  void setBluetooth(bool on) override;
  void setGps(bool on) override;
  bool advertLocation() override;
  void setAdvertLocation(bool on) override;
  DeviceStatus status() override;
  UiSettings& ui() override { return _ui; }
  void saveUi() override;

  void setBacklight(uint8_t level) override;
  void setKeyboardLight(bool on) override;
  void playSound(SoundKind k) override;
  void notifyFlash() override;
  void loopFlash();
  void rebootToLauncher() override;
  void factoryReset() override;
  const char* diagnostics() override;
  bool readTextFile(const char* path, char* buf, int max) override;
  bool writeTextFile(const char* path, const char* text) override;
  bool renameFile(const char* from, const char* to) override;
  int  selfTest(SelfTestItem* out, int max) override;
  const char* lastResetReason() override;
  bool crashedLastBoot() override;
  uint32_t inputEdges(int dir) override;
  const char* tileRoot() override { return "S:/sd/tiles"; }
  void trackAppend(const TrackPoint& p) override;
  int  trackLoad(TrackPoint* out, int max) override;
  void trackClear() override;

private:
  void saveContacts();
  void saveChannels();
  MessageLog* _log = nullptr;
  MultiSerialInterface* _serial = nullptr;
  BaseSerialInterface* _ble = nullptr;
  DataStore* _store = nullptr;
  UiSettings _ui;
  bool _bt_on = true;
  uint32_t _batt_at = 0;
  // admin session (one repeater at a time)
  uint8_t _adm_key[LOG_KEY_LEN] = {0};
  AdminState _adm_state = ADM_NONE;
  uint32_t _adm_ms = 0, _status_tag = 0;
  RepeaterStatus _status = {};
  #define CLI_LINES 40
  CliLine _cli[CLI_LINES];
  int _cli_n = 0;
  void cliPush(bool out, const char* t);
  bool isAdmKey(const uint8_t* k) const { return memcmp(k, _adm_key, LOG_KEY_LEN) == 0; }
  char _adm_pw[16] = "";
  struct RoomLogin { uint8_t key[LOG_KEY_LEN]; char pw[16]; };
  #define MAX_ROOMS 8
  RoomLogin _rooms[MAX_ROOMS] = {};
  int _n_rooms = 0, _room_next = 0;
  uint32_t _room_ms = 0;
  bool _rooms_loaded = false;
  void saveRooms();
  void rememberRoom(const uint8_t* key, const char* pw);
  struct Loc { uint8_t key[LOG_KEY_LEN]; double lat, lon; uint32_t at; };
  Loc _locs[16] = {};
  int _n_locs = 0;
  const Loc* findLoc(const uint8_t* key) const;
  void applyLoc(ContactView& v) const;
  int _batt_mv = 0, _batt_pct = 0;
};

extern DeviceModel device_model;
