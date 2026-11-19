#pragma once
// Model — everything the UI needs from the device, as one abstract interface.
// The firmware implements it on top of MeshCore (device/DeviceModel); the desktop
// simulator implements it with fake data (sim/SimModel). The UI never touches
// MeshCore, Arduino or hardware directly.
#include "Types.h"

struct ContactView {
  uint8_t key[LOG_KEY_LEN];
  char name[LOG_NAME_MAX];
  uint8_t type;          // 1 chat, 2 repeater, 3 room, 4 sensor
  bool favorite;
  bool share_location;   // we allow this contact to request our location
  uint32_t last_heard;   // unix time (0 = never)
  bool has_location;
  double lat, lon;
  uint32_t location_time;
  uint8_t hops;          // 0xFF = unknown / flood
};

struct RadioStats {
  uint32_t rx, tx, rx_errors;
  float last_rssi, last_snr;
  int noise_floor;
  uint32_t uptime_s;
};

struct PacketView {
  uint32_t ago_s;        // seconds since received
  float snr, rssi;
  uint8_t len;
  uint8_t type;          // MeshCore payload type (4 = advert, 2 = DM, 5 = group text, 3 = ack ...)
  uint8_t flood;         // 1 flood, 0 direct
  uint8_t hops;
  uint8_t scoped;        // carried region transport codes
};

struct TrackPoint { float lat, lon; uint32_t t; };

static const char* const kThemeNamesPlain[] = { "tactical", "modern", "modern-light" };

struct DiscoveredView {
  uint8_t key[LOG_KEY_LEN];
  char name[LOG_NAME_MAX];   // "" if not in contacts
  uint8_t type;              // 2 repeater, 4 sensor ...
  float snr_them, snr_us;
};

enum AdminState : uint8_t { ADM_NONE = 0, ADM_PENDING, ADM_GUEST, ADM_ADMIN, ADM_FAILED };

struct RepeaterStatus {
  bool valid;
  uint32_t at;               // unix time received
  uint16_t batt_mv, tx_queue;
  int16_t noise_floor, last_rssi;
  float last_snr;
  uint32_t pkts_recv, pkts_sent, air_secs, uptime_secs;
  uint32_t sent_flood, sent_direct, recv_flood, recv_direct;
  uint16_t err_events, direct_dups, flood_dups;
};

struct CliLine { bool out; char text[LOG_TEXT_MAX + 1]; };

// GPS detail (speed, course, satellites in view for the sky plot)
#define GPS_MAX_SATS 32
struct GpsSat { uint8_t prn, elev, snr; uint16_t az; char sys; };   // sys: G GPS, R GLONASS, E Galileo, C BeiDou
struct GpsDetail {
  bool enabled = false, valid = false;
  double lat = 0, lon = 0;
  float alt_m = 0, speed_kmh = 0, course_deg = 0, hdop = 0;
  int sats_used = 0, sats_view = 0, fix_type = 0;   // fix_type: 1 none, 2 2D, 3 3D
  uint32_t fix_age_s = 0xFFFFFFFF, sentences = 0;
  int n = 0;
  GpsSat sats[GPS_MAX_SATS];
};

// Saved places
#define MAX_WAYPOINTS 32
struct Waypoint { char name[20]; double lat, lon; uint32_t at; };

// Wi-Fi + offline map download
struct WifiNet { char ssid[33]; int rssi; bool secure; };
enum TileDlState : uint8_t { DL_IDLE = 0, DL_CONNECTING, DL_RUNNING, DL_DONE, DL_FAILED, DL_CANCELLED };
struct TileDlStatus {
  uint8_t state = DL_IDLE;
  uint32_t total = 0, done = 0, fetched = 0, skipped = 0, failed = 0;
  uint32_t bytes = 0;
  char msg[64] = "";
};

enum TestResult : uint8_t { TEST_PASS = 0, TEST_WARN = 1, TEST_FAIL = 2 };
struct SelfTestItem { const char* name; TestResult result; char detail[48]; };

struct HeardView {
  char name[LOG_NAME_MAX];
  uint8_t key[LOG_KEY_LEN];   // 7-byte prefix in MeshCore; first 6 used
  uint32_t when;              // unix time
  uint8_t hops;               // 0 = direct
};

struct ChannelView {
  uint8_t key[LOG_KEY_LEN];
  char name[LOG_NAME_MAX];
  int idx;
};

struct RadioPreset {
  const char* name;
  const char* where;     // short description of who uses it
  float freq, bw;
  uint8_t sf, cr;
};

struct RadioSettings {
  float freq, bw;
  uint8_t sf, cr;
  int8_t tx_power;
};

struct DeviceStatus {
  int battery_pct;
  int battery_mv;
  bool charging;
  bool bt_enabled;
  bool bt_connected;
  bool gps_enabled;
  bool gps_fix;
  int gps_sats;
  double lat, lon;
  bool sd_card;
  bool under_launcher;
  uint32_t now;          // unix time
  bool clock_valid;
  uint32_t free_heap_kb, free_psram_kb;
  int contacts;
  uint32_t messages_stored;
};

// EasyDeck's own UI settings (persisted by the model)
struct UiSettings {
  uint8_t theme = 0;            // 0 dark, 1 light
  uint8_t easy_mode = 0;        // big buttons + huge text for non-technical users
  uint8_t text_size = 0;        // 0 normal, 1 large
  uint8_t brightness = 12;      // 1..16
  uint8_t screen_timeout = 1;   // index into kScreenTimeouts
  uint8_t sound = 1;
  uint8_t kb_light = 1;
  uint8_t show_all_contacts = 1;
  uint8_t setup_done = 0;
  // SOS state survives reboots
  uint8_t sos_active = 0;
  uint8_t sos_seq = 0;
  uint8_t sos_post_emergency = 0;   // also post to #emergency
  uint8_t sos_reserved = 0;
  uint32_t sos_started = 0;         // unix time
  uint8_t auto_advert = 3;          // index into kAutoAdvertHours (default 6 h)
  uint8_t track_on = 0;             // record a GPS breadcrumb track
  uint8_t share_loc = 0;            // index into kShareMinutes: send my position to favourites
  uint8_t bt_off = 0;               // Bluetooth switched off by the user
  uint8_t reserved[11] = {0};
};

static const uint32_t kScreenTimeoutSec[] = { 15, 30, 60, 120, 300, 0 };
static const char* const kScreenTimeoutNames[] = { "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes", "Never" };
#define NUM_SCREEN_TIMEOUTS 6

static const uint16_t kAutoAdvertHours[] = { 0, 1, 3, 6, 12, 24 };
static const char* const kAutoAdvertNames[] = { "Off", "Every hour", "Every 3 hours", "Every 6 hours", "Every 12 hours", "Daily" };
#define NUM_AUTO_ADVERT 6

static const uint16_t kShareMinutes[] = { 0, 5, 15, 30, 60 };
static const char* const kShareNames[] = { "Off", "Every 5 min", "Every 15 min", "Every 30 min", "Every hour" };
#define NUM_SHARE 5

enum SoundKind { SOUND_MESSAGE = 0, SOUND_CLICK = 1, SOUND_SENT = 2, SOUND_ALERT = 3 };

class Model {
public:
  virtual ~Model() {}

  // ---- messages ----
  virtual int  listConversations(ConvSummary* out, int max) = 0;
  virtual int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) = 0;
  virtual bool getMessage(uint32_t seq, LogRecord& out) = 0;
  virtual int  unreadCount(const ConvKey& k) = 0;
  virtual int  totalUnread() = 0;
  virtual void markRead(const ConvKey& k) = 0;
  virtual bool sendText(const ConvKey& k, const char* text) = 0;
  virtual int  maxTextLen(const ConvKey& k) = 0;

  // ---- contacts & groups ----
  virtual int  contactCount() = 0;
  virtual bool contactAt(int i, ContactView& out) = 0;
  virtual bool findContact(const uint8_t* key, ContactView& out) = 0;
  virtual void setFavorite(const uint8_t* key, bool fav) = 0;
  virtual void setShareLocation(const uint8_t* key, bool share) = 0;
  virtual bool removeContact(const uint8_t* key) = 0;
  virtual bool requestLocation(const uint8_t* key) = 0;
  virtual void noteLocation(const uint8_t* key, double lat, double lon) = 0;   // learned from a message
  virtual bool resetRoute(const uint8_t* key) = 0;       // forget the learned path (next send floods)
  virtual bool shareContact(const uint8_t* key) = 0;     // re-broadcast their advert to neighbours
  virtual int  channelCount() = 0;
  virtual bool channelAt(int i, ChannelView& out) = 0;
  virtual bool findChannel(const uint8_t* key, ChannelView& out) = 0;
  virtual bool joinChannel(const char* hashtag_name) = 0;
  virtual bool leaveChannel(const uint8_t* key) = 0;
  // private groups: 16-byte secret shared as 32 hex characters
  virtual bool createPrivateChannel(const char* name, char* hex_out33) = 0;
  virtual bool joinPrivateChannel(const char* name, const char* hex32) = 0;
  virtual bool channelSecretHex(const uint8_t* key, char* hex_out33) = 0;
  // per-group region scope (MeshCore only has one default): mode 0 = use default, 1 = unscoped, 2 = named
  virtual void setChannelScope(const uint8_t* key, int mode, const char* name) = 0;
  virtual int  channelScope(const uint8_t* key, char* name_out32) = 0;
  virtual void advertise(bool far) = 0;   // far = flood through repeaters
  // repeaters heard re-sending my last flood advert (active = an advert was sent this boot)
  virtual void advertEcho(bool& active, uint32_t& age_s, int& count) = 0;
  virtual RadioStats radioStats() = 0;
  virtual int  recentlyHeard(HeardView* out, int max) = 0;
  virtual int  packetLog(PacketView* out, int max) = 0;   // newest first
  // ---- repeater / room admin (free, unlike Ripple) ----
  virtual bool adminLogin(const uint8_t* key, const char* password) = 0;   // "" = guest / ACL
  virtual AdminState adminState(const uint8_t* key) = 0;
  virtual bool requestStatus(const uint8_t* key) = 0;
  virtual bool getStatus(const uint8_t* key, RepeaterStatus& out) = 0;
  virtual bool sendCli(const uint8_t* key, const char* cmd) = 0;
  virtual int  cliLines(const uint8_t* key, CliLine* out, int max) = 0;   // oldest first
  virtual bool discover() = 0;                            // ask nearby repeaters to answer
  virtual int  discovered(DiscoveredView* out, int max) = 0;

  // ---- identity & settings ----
  virtual const char* nodeName() = 0;
  virtual void setNodeName(const char* name) = 0;
  virtual void nodeId(char* out, int n) = 0;            // short hex id
  virtual RadioSettings radio() = 0;
  virtual void setRadio(const RadioPreset& p) = 0;
  virtual const char* regionScope() = 0;                // "" = none
  virtual void setRegionScope(const char* scope) = 0;
  virtual int  tzOffset() = 0;                          // hours
  virtual void setTzOffset(int hours) = 0;
  virtual void setTime(uint32_t unix_utc) = 0;          // manual clock set
  virtual uint32_t blePin() = 0;
  virtual void setBluetooth(bool on) = 0;
  virtual void setGps(bool on) = 0;
  virtual bool advertLocation() = 0;                    // include my position in adverts
  virtual void setAdvertLocation(bool on) = 0;
  virtual DeviceStatus status() = 0;
  virtual UiSettings& ui() = 0;
  virtual void saveUi() = 0;

  // ---- hardware ----
  virtual void setBacklight(uint8_t level) = 0;         // 0 = off
  virtual void setKeyboardLight(bool on) = 0;
  virtual void playSound(SoundKind k) = 0;
  virtual void notifyFlash() = 0;                       // blink the keyboard light (new message, screen off)
  virtual void rebootToLauncher() = 0;
  virtual void factoryReset() = 0;
  virtual const char* diagnostics() = 0;
  // small text files on the SD card (paths relative to the card root)
  virtual bool readTextFile(const char* path, char* buf, int max) = 0;   // NUL-terminated
  virtual bool writeTextFile(const char* path, const char* text) = 0;
  virtual bool renameFile(const char* from, const char* to) = 0;
  virtual int  selfTest(SelfTestItem* out, int max) = 0;   // hardware checklist for Device check
  virtual const char* lastResetReason() = 0;            // "" for a normal power-on
  virtual bool crashedLastBoot() = 0;
  virtual uint32_t inputEdges(int dir) = 0;             // trackball diagnostics
  virtual const char* tileRoot() = 0;
  virtual void gpsDetail(GpsDetail& out) = 0;
  virtual int  waypointCount() = 0;
  virtual bool waypointAt(int i, Waypoint& out) = 0;
  virtual bool addWaypoint(const Waypoint& w) = 0;   // false when full
  virtual void removeWaypoint(int i) = 0;

  // Wi-Fi (only switched on while scanning or downloading)
  virtual bool wifiScanStart() = 0;
  virtual int  wifiScanResults(WifiNet* out, int max) = 0;   // -1 while scanning
  virtual void setWifi(const char* ssid, const char* pass) = 0;
  virtual const char* wifiSsid() = 0;                         // "" = not set
  // tile source: URL template with {z} {x} {y}, e.g. https://host/{z}/{x}/{y}.png?apikey=...
  virtual void setTileUrl(const char* url) = 0;
  virtual const char* tileUrl() = 0;
  virtual bool hasSd() = 0;
  virtual bool tileDownloadStart(double lat, double lon, double radius_km, int zmin, int zmax) = 0;
  virtual void tileDownloadCancel() = 0;
  virtual TileDlStatus tileDownloadStatus() = 0;
  // GPS breadcrumb track persistence
  virtual void trackAppend(const TrackPoint& p) = 0;
  virtual int  trackLoad(TrackPoint* out, int max) = 0;   // oldest first
  virtual void trackClear() = 0;                   // LVGL path of the map tile folder, e.g. "S:/sd/tiles"
};

// Presets offered in the UI (defined in core/Presets.cpp)
extern const RadioPreset kRadioPresets[];
extern const int kNumRadioPresets;
int findPreset(const RadioSettings& r);   // -1 = custom
