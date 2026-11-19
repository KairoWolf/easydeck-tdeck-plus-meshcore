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

// Route to a contact: the repeaters a direct message goes through (learned by MeshCore)
struct PathHop { uint8_t hash[4]; uint8_t hash_len; char name[24]; uint8_t matches; };   // matches: contacts with this hash

// Owner / emergency info (readable from the lock screen, like a phone's Medical ID)
struct OwnerInfo { char owner[32]; char ice[40]; char medical[100]; };

// Route ping (MeshCore TRACE along the learned route and back)
#define TRACE_MAX_HOPS 15
struct TraceResult {
  uint8_t state = 0;            // 0 idle, 1 waiting, 2 done, 3 no answer, 4 couldn't send
  uint8_t hops = 0;             // links measured (out + back + final)
  int8_t snr4[TRACE_MAX_HOPS + 1] = {};   // SNR*4 per link, in order
  uint32_t rtt_ms = 0;
  uint8_t key[LOG_KEY_LEN] = {};   // the contact this ping was for
};

// Which new nodes become contacts automatically (MeshCore 1.18 auto-add prefs)
struct AutoAddCfg {
  uint8_t mode = 0;        // 0 everyone, 1 people only, 2 people + repeaters/rooms, 3 nobody (add from Heard)
  uint8_t max_hops = 0;    // 0 no limit, 1 direct only, N = up to N-1 hops
  bool overwrite = false;  // list full: replace the oldest non-favourite
};

enum { TM_DENY = 0, TM_CHOSEN = 1, TM_ALL = 2 };
#define TZ_AU_EAST 101   // NSW, VIC, ACT, TAS: UTC+10, +11 from the 1st Sunday of October to the 1st Sunday of April
int tzOffsetAt(int setting, uint32_t unix_utc);   // core/Time.cpp   // same values as MeshCore TELEM_MODE_*

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

// Software update from the GitHub releases page (Settings > Software update)
enum UpdState : uint8_t { UPD_IDLE = 0, UPD_CHECKING, UPD_LATEST, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_READY, UPD_FAILED };
struct UpdateStatus {
  uint8_t state = UPD_IDLE;
  char version[16] = "";     // newest release, e.g. "0.48.0"
  char file[40] = "";        // on the SD card once downloaded, e.g. "/EasyDeck-0.48.0-full.bin"
  char notes[240] = "";      // what's new (plain text, first lines of the release notes)
  char msg[64] = "";
  uint32_t size = 0, done = 0;
  bool launcher = false;     // running under the SD Launcher (can hand over to install)
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
  uint8_t desk_mode = 0;            // bits 0-1: desk mode (0 off, 1 charger, 2 charger or still); bit 7: don't use the mesh.nah.rocks mirror
#define DESK_MASK   0x03
#define NO_MIRROR   0x80
  uint32_t sos_started = 0;         // unix time
  uint8_t auto_advert = 3;          // index into kAutoAdvertHours (default 6 h)
  uint8_t track_on = 0;             // record a GPS breadcrumb track
  uint8_t share_loc = 0;            // index into kShareMinutes: send my position to favourites
  uint8_t bt_off = 0;               // Bluetooth switched off by the user
  uint8_t pin_hash[4] = {0};        // screen lock: hash of the PIN/passphrase, all zero = off
  uint8_t battery_saver = 0;        // power use: 0 normal, 1 battery saver (light sleep, screen off), 2 maximum saving
  uint8_t tone = 0;                 // message tone (index < NUM_TONES)
  uint8_t group_tone = 0;           // 0 same as messages, 1 short tick, 2 silent
  uint8_t shot_no[2] = {0};         // next screenshot number (little endian), so boot needn't scan the card
  uint8_t backup_off = 0;           // 1 = don't back up identity/contacts/settings to the SD card
  uint8_t defaults_v = 0;           // one-time default changes already applied (last spare byte)
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

enum SoundKind { SOUND_MESSAGE = 0, SOUND_CLICK = 1, SOUND_SENT = 2, SOUND_ALERT = 3, SOUND_GROUP = 4,
                 SOUND_SOS_ALARM = 5 };   // SOS_ALARM: someone else's SOS - siren, plays even with sounds off
#define NUM_TONES 3          // message tone choices: chime, double beep, soft ping
#define NUM_GROUP_TONES 3    // group messages: same as messages, short tick, silent

class Model {
public:
  virtual ~Model() {}

  // ---- messages ----
  virtual int  listConversations(ConvSummary* out, int max) = 0;
  virtual int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) = 0;
  // messages whose text or sender name contains q (any case), newest first, recent window
  virtual int  searchMessages(const char* q, uint32_t* seqs, int max) = 0;
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
  // muted groups: messages still arrive and count as unread, but no sound / banner / wake
  // private notes about a contact (only on this device), "" = none
  #define CONTACT_NOTE_MAX 56
  virtual void contactNote(const uint8_t* key, char* out /*CONTACT_NOTE_MAX+1*/) = 0;
  virtual bool setContactNote(const uint8_t* key, const char* note) = 0;   // false: notebook full
  virtual bool channelMuted(const uint8_t* key) = 0;
  virtual bool setChannelMuted(const uint8_t* key, bool muted) = 0;   // false: mute list full
  virtual void advertise(bool far) = 0;   // far = flood through repeaters
  // repeaters heard re-sending my last flood advert (active = an advert was sent this boot)
  virtual void advertEcho(bool& active, uint32_t& age_s, int& count) = 0;
  virtual RadioStats radioStats() = 0;
  virtual int  recentlyHeard(HeardView* out, int max) = 0;
  virtual bool canAddHeard(const uint8_t* key) = 0;   // heard but not auto-added: we kept its advert
  virtual bool addHeard(const uint8_t* key) = 0;
  virtual AutoAddCfg autoAdd() = 0;
  virtual void setAutoAdd(const AutoAddCfg& c) = 0;
  // Who may request our telemetry (MeshCore telemetry_mode_*): TM_DENY / TM_CHOSEN / TM_ALL.
  // loc=false: battery & status (base), loc=true: GPS position. Position needs base too.
  // SD backup of internal data: status text for Settings ("Saved 5 min ago", "No SD card", ...)
  virtual void backupInfo(char* out, int n) = 0;
  virtual uint8_t telemetryMode(bool loc) = 0;
  virtual void setTelemetryMode(bool loc, uint8_t mode) = 0;
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
  virtual void setRadioCustom(const RadioSettings& r) = 0;   // own frequency / bandwidth / SF / CR / TX power
  // path hash size in bytes (1-3): how repeaters are identified in the paths of packets we send
  virtual int  pathHashBytes() = 0;
  virtual void setPathHashBytes(int bytes) = 0;
  virtual const char* regionScope() = 0;                // "" = none
  virtual void setRegionScope(const char* scope) = 0;
  virtual int  tzOffset() = 0;                          // hours, now (daylight saving applied for TZ_AU_EAST)
  virtual int  tzSetting() = 0;                         // what was chosen: hours, or TZ_AU_EAST
  virtual void setTzOffset(int hours) = 0;
  virtual void setTime(uint32_t unix_utc) = 0;          // manual clock set
  virtual const char* clockSource() = 0;                // "GPS or phone", "internet", "mesh", "set by hand", "not set"
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
  virtual bool writeFile(const char* path, const uint8_t* data, size_t len) = 0;   // SD, creates the folder
  virtual bool fileExists(const char* path) = 0;
  virtual bool renameFile(const char* from, const char* to) = 0;
  virtual int  selfTest(SelfTestItem* out, int max) = 0;   // hardware checklist for Device check
  virtual const char* lastResetReason() = 0;            // "" for a normal power-on
  virtual bool crashedLastBoot() = 0;
  virtual uint32_t inputEdges(int dir) = 0;             // trackball diagnostics
  virtual const char* tileRoot() = 0;
  virtual void gpsDetail(GpsDetail& out) = 0;
  virtual void ownerInfo(OwnerInfo& out) = 0;
  virtual void setOwnerInfo(const OwnerInfo& in) = 0;
  // -1 = no route learned (messages flood), else the number of hops written (0 = direct)
  virtual int  contactPath(const uint8_t* key, PathHop* out, int max) = 0;
  virtual int  messageRepeaters(const LogRecord& r, PathHop* out, int max) = 0;   // names for r.rpt
  virtual bool startTrace(const uint8_t* key) = 0;   // ping the learned route to this contact
  virtual TraceResult traceResult() = 0;
  virtual int  waypointCount() = 0;
  virtual bool waypointAt(int i, Waypoint& out) = 0;
  virtual bool addWaypoint(const Waypoint& w) = 0;   // false when full
  virtual void removeWaypoint(int i) = 0;

  // Wi-Fi (only switched on while scanning or downloading)
  // mesh.nah.rocks mirror (Sydney) for updates and Thunderforest map tiles; falls back to direct
  virtual bool useMirror() = 0;
  virtual void setUseMirror(bool on) = 0;
  virtual bool updateCheck() = 0;      // connect to the saved Wi-Fi, ask GitHub for the newest release
  virtual bool updateDownload() = 0;   // fetch it to the SD card (SHA-256 checked)
  virtual void updateCancel() = 0;
  virtual UpdateStatus updateStatus() = 0;
  virtual void updateInstall() = 0;    // restart into the SD Launcher
  // small text files from GitHub (settings packs) over the saved Wi-Fi
  // Wi-Fi mode: Wi-Fi needs internal RAM that Bluetooth holds. If there isn't enough, EasyDeck
  // restarts without Bluetooth (radio + messages keep working) and opens the screen that needed it.
  enum { WIFI_FOR_UPDATE = 1, WIFI_FOR_MAPS = 2, WIFI_FOR_PACKS = 3 };
  virtual bool wifiMode() = 0;                 // this boot skipped Bluetooth for Wi-Fi
  virtual bool wifiMemoryOk() = 0;             // enough internal RAM for Wi-Fi + HTTPS now
  virtual void restartForWifi(int target) = 0;
  virtual void restartNormal() = 0;
  virtual int  wifiModeTarget() = 0;           // screen to open after a Wi-Fi mode boot (once)
  virtual bool netFetch(const char* url) = 0;
  virtual int  netFetchState() = 0;          // 0 idle, 1 busy, 2 done, 3 failed
  virtual const char* netFetchText() = 0;    // the file (done) or the error message (failed)
  virtual bool wifiScanStart() = 0;
  virtual int  wifiScanResults(WifiNet* out, int max) = 0;   // -1 while scanning
  virtual void wifiScanStop() = 0;
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
