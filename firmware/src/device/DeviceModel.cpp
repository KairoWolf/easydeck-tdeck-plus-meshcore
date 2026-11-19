#include "DeviceModel.h"
#include <esp_heap_caps.h>
#include "store/Backup.h"
#include "Updater.h"
#include "NetTime.h"
#include "Crash.h"
#include <Arduino.h>
#include <stdarg.h>
#include <SPIFFS.h>
#include "EasyMesh.h"
#include "store/MessageLog.h"
#include "store/Storage.h"
#include "TileDownloader.h"
#include "GpsTap.h"
#include <esp_attr.h>
#include <esp_system.h>

#define CLOCK_TRUST_MAGIC 0x434C4B54UL
static RTC_NOINIT_ATTR uint32_t s_clock_trusted;
static RTC_NOINIT_ATTR uint32_t s_wifi_req;   // WIFI_MAGIC | target in the low byte
#define WIFI_MAGIC 0x57494600UL               // "WIF"
bool DeviceModel::s_wifi_boot = false;
int DeviceModel::s_wifi_target = 0;
void deviceUiContactsChanged();   // DeviceUI.cpp
#include <SD.h>
#include "hal/Display.h"
#include "hal/Input.h"
#include "hal/Sound.h"
#include <helpers/MultiSerialInterface.h>
#include <helpers/AdvertDataHelpers.h>

DeviceModel device_model;

#define UI_FILE   "/easydeck_ui2"
#define UI_MAGIC  0x45445532   // "EDU2"

void DeviceModel::begin(MessageLog* log, MultiSerialInterface* serial, DataStore* store, BaseSerialInterface* ble) {
  _clk_ref_rtc = rtc_clock.getCurrentTime();
  _clk_ref_ms = millis();
  // a trusted clock survives soft resets (crash, watchdog, launcher) via RTC memory
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT) s_clock_trusted = 0;
  _clock_trusted = s_clock_trusted == CLOCK_TRUST_MAGIC;
  _log = log;
  _serial = serial;
  _ble = ble;
  _store = store;
  UiSettings tmp;
  if (safeLoad(DataFS, UI_FILE, UI_MAGIC, &tmp, sizeof(tmp))) _ui = tmp;
  migrateDefaultScope();
  updater.setMirror(useMirror());
  tile_downloader.setMirror(useMirror());
  if (_ui.defaults_v < 1) {   // 0.49: new contacts are only added when you add them (MeshCore: everyone)
    AutoAddCfg c = autoAdd();
    if (c.mode == 0) { c.mode = 3; setAutoAdd(c); }
    _ui.defaults_v = 1;
    saveUi();
  }
}

// Scopes are per channel now. An old global default scope becomes each channel's own scope
// (same behaviour as before), then the default is cleared.
void DeviceModel::migrateDefaultScope() {
  const char* def = regionScope();
  if (!*def) return;
  char name[32];
  snprintf(name, sizeof(name), "%s", def);
  for (int i = 0; i < channelCount(); i++) {
    ChannelView ch;
    char cur[32];
    if (channelAt(i, ch) && channelScope(ch.key, cur) == 0) setChannelScope(ch.key, 2, name);
  }
  setRegionScope("");
}

void DeviceModel::saveUi() {
  safeSave(DataFS, UI_FILE, UI_MAGIC, &_ui, sizeof(_ui));   // two copies: a reset mid-write can't lose an active SOS
}

void DeviceModel::saveContacts() { _store->saveContacts(&easy_mesh); }
void DeviceModel::saveChannels() { _store->saveChannels(&easy_mesh); }

// ---------------------------------------------------------------- messages

int DeviceModel::listConversations(ConvSummary* out, int max) { return _log->listConversations(out, max); }
int DeviceModel::conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) { return _log->conversationSeqs(k, seqs, max); }
int DeviceModel::searchMessages(const char* q, uint32_t* seqs, int max) { return _log->search(q, seqs, max); }
bool DeviceModel::getMessage(uint32_t seq, LogRecord& out) { return _log->get(seq, out); }
int DeviceModel::unreadCount(const ConvKey& k) { return _log->unreadCount(k); }
int DeviceModel::totalUnread() { return _log->totalUnread(); }
void DeviceModel::markRead(const ConvKey& k) { _log->markConversationRead(k); }
bool DeviceModel::sendText(const ConvKey& k, const char* text) { return easy_mesh.sendFromDevice(k, text) >= 0; }

int DeviceModel::maxTextLen(const ConvKey& k) {
  if (k.kind == KIND_CHANNEL) {
    int m = 150 - (int)strlen(easy_mesh.getNodeName()) - 2;
    return m < 20 ? 20 : m;
  }
  return 150;
}

// ---------------------------------------------------------------- contacts

static void toView(const ContactInfo& c, ContactView& v) {
  memset(&v, 0, sizeof(v));
  memcpy(v.key, c.id.pub_key, LOG_KEY_LEN);
  strncpy(v.name, c.name, sizeof(v.name) - 1);
  v.type = c.type;
  v.favorite = c.isFav();
  v.share_location = c.isTelemLocAllowed();
  v.last_heard = c.lastmod;
  v.has_location = c.gps_lat != 0 || c.gps_lon != 0;
  v.lat = c.gps_lat / 1e6;
  v.lon = c.gps_lon / 1e6;
  v.hops = c.out_path_len == OUT_PATH_UNKNOWN ? 0xFF : (c.out_path_len & 0x3F);
}

const DeviceModel::Loc* DeviceModel::findLoc(const uint8_t* key) const {
  for (int i = 0; i < _n_locs; i++) if (memcmp(_locs[i].key, key, LOG_KEY_LEN) == 0) return &_locs[i];
  return nullptr;
}

void DeviceModel::noteLocation(const uint8_t* key, double lat, double lon) {
  Loc* l = (Loc*)findLoc(key);
  if (!l) {
    if (_n_locs < 16) l = &_locs[_n_locs++];
    else { memmove(&_locs[0], &_locs[1], sizeof(Loc) * 15); l = &_locs[15]; }
    memcpy(l->key, key, LOG_KEY_LEN);
  }
  l->lat = lat; l->lon = lon; l->at = rtc_clock.getCurrentTime();
}

void DeviceModel::applyLoc(ContactView& v) const {
  const Loc* l = findLoc(v.key);
  if (l) { v.has_location = true; v.lat = l->lat; v.lon = l->lon; v.location_time = l->at; }
}

int DeviceModel::contactCount() { return easy_mesh.getNumContacts(); }

bool DeviceModel::contactAt(int i, ContactView& out) {
  ContactInfo c;
  if (i < 0 || !easy_mesh.getContactByIdx(i + MAX_ANON_CONTACTS, c)) return false;   // slots 0..7 are reserved
  toView(c, out);
  applyLoc(out);
  return true;
}

bool DeviceModel::findContact(const uint8_t* key, ContactView& out) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return false;
  toView(*c, out);
  applyLoc(out);
  return true;
}

void DeviceModel::setFavorite(const uint8_t* key, bool fav) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return;
  if (fav) c->flags |= 0x01; else c->flags &= ~0x01;
  saveContacts();
}

void DeviceModel::setShareLocation(const uint8_t* key, bool share) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return;
  if (share) c->flags |= 0x04 | 0x02; else c->flags &= ~(0x04 | 0x02);   // location + battery telemetry
  // Sharing with someone needs at least "people I choose"; never narrows an "everyone" setting.
  NodePrefs* p = easy_mesh.getNodePrefs();
  if (share && (p->telemetry_mode_loc == TELEM_MODE_DENY || p->telemetry_mode_base == TELEM_MODE_DENY)) {
    if (p->telemetry_mode_loc == TELEM_MODE_DENY) p->telemetry_mode_loc = TELEM_MODE_ALLOW_FLAGS;
    if (p->telemetry_mode_base == TELEM_MODE_DENY) p->telemetry_mode_base = TELEM_MODE_ALLOW_FLAGS;
    easy_mesh.savePrefs();
  }
  saveContacts();
}

bool DeviceModel::removeContact(const uint8_t* key) {
  bool ok = easy_mesh.removeContactByKey(key);
  if (ok) { saveContacts(); setContactNote(key, ""); deviceUiContactsChanged(); }
  return ok;
}

bool DeviceModel::requestLocation(const uint8_t* key) { return easy_mesh.requestTelemetry(key); }

bool DeviceModel::resetRoute(const uint8_t* key) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return false;
  easy_mesh.resetPathTo(*c);
  saveContacts();
  return true;
}

bool DeviceModel::shareContact(const uint8_t* key) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  return c && easy_mesh.shareContactZeroHop(*c);
}

// ---------------------------------------------------------------- channels

int DeviceModel::channelCount() {
  int n = 0;
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) if (easy_mesh.getChannel(i, ch) && ch.name[0]) n++;
  return n;
}

bool DeviceModel::channelAt(int idx, ChannelView& out) {
  int n = 0;
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!easy_mesh.getChannel(i, ch) || !ch.name[0]) continue;
    if (n++ == idx) {
      memset(&out, 0, sizeof(out));
      memcpy(out.key, ch.channel.secret, LOG_KEY_LEN);
      strncpy(out.name, ch.name, sizeof(out.name) - 1);
      out.idx = i;
      return true;
    }
  }
  return false;
}

bool DeviceModel::findChannel(const uint8_t* key, ChannelView& out) {
  ChannelDetails ch;
  int i = easy_mesh.findChannelByKey(key, &ch);
  if (i < 0) return false;
  memset(&out, 0, sizeof(out));
  memcpy(out.key, ch.channel.secret, LOG_KEY_LEN);
  strncpy(out.name, ch.name, sizeof(out.name) - 1);
  out.idx = i;
  return true;
}

bool DeviceModel::joinChannel(const char* name) {
  if (easy_mesh.joinHashtagChannel(name) < 0) return false;
  saveChannels();
  return true;
}

bool DeviceModel::leaveChannel(const uint8_t* key) {
  if (!easy_mesh.leaveChannelByKey(key)) return false;
  saveChannels();
  return true;
}

static void toHex(const uint8_t* b, int n, char* out) {
  static const char* d = "0123456789abcdef";
  for (int i = 0; i < n; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 15]; }
  out[2 * n] = 0;
}

static bool fromHex(const char* s, uint8_t* out, int n) {
  auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
  int k = 0;
  for (const char* p = s; *p && k < 2 * n; p++) {
    if (*p == ' ' || *p == '-' || *p == ':') continue;
    int x = v(*p);
    if (x < 0) return false;
    if (k % 2 == 0) out[k / 2] = x << 4; else out[k / 2] |= x;
    k++;
  }
  return k == 2 * n;
}

bool DeviceModel::createPrivateChannel(const char* name, char* hex) {
  uint8_t secret[16];
  esp_fill_random(secret, sizeof(secret));
  if (easy_mesh.joinSecretChannel(name, secret) < 0) return false;
  saveChannels();
  toHex(secret, 16, hex);
  return true;
}

bool DeviceModel::joinPrivateChannel(const char* name, const char* hex) {
  uint8_t secret[16];
  if (!fromHex(hex, secret, 16)) return false;
  if (easy_mesh.joinSecretChannel(name, secret) < 0) return false;
  saveChannels();
  return true;
}

void DeviceModel::easy_mesh_setChannelScope(const uint8_t* key, int mode, const char* name) { easy_mesh.setChannelScope(key, mode, name); }
int DeviceModel::easy_mesh_channelScope(const uint8_t* key, char* out) { return easy_mesh.channelScope(key, out); }

bool DeviceModel::channelSecretHex(const uint8_t* key, char* hex) {
  ChannelDetails ch;
  if (easy_mesh.findChannelByKey(key, &ch) < 0) return false;
  toHex(ch.channel.secret, 16, hex);
  return true;
}

void DeviceModel::advertise(bool far) {
  if (far) easy_mesh.advertFlood();
  else easy_mesh.advert();
}

RadioStats DeviceModel::radioStats() {
  RadioStats r = {};
  r.rx = radio_driver.getPacketsRecv();
  r.tx = radio_driver.getPacketsSent();
  r.rx_errors = radio_driver.getPacketsRecvErrors();
  r.last_rssi = radio_driver.getLastRSSI();
  r.last_snr = radio_driver.getLastSNR();
  r.noise_floor = radio_driver.getNoiseFloor();
  r.uptime_s = millis() / 1000;
  return r;
}

int DeviceModel::recentlyHeard(HeardView* out, int max) {
  AdvertPath paths[16];
  int n = easy_mesh.getRecentlyHeard(paths, max < 16 ? max : 16);
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (paths[i].recv_timestamp == 0 || paths[i].name[0] == 0) continue;
    HeardView& h = out[k++];
    memset(&h, 0, sizeof(h));
    strncpy(h.name, paths[i].name, sizeof(h.name) - 1);
    memcpy(h.key, paths[i].pubkey_prefix, LOG_KEY_LEN);
    h.when = paths[i].recv_timestamp;
    h.hops = paths[i].path_len & 0x3F;
  }
  return k;
}

int DeviceModel::packetLog(PacketView* out, int max) {
  EasyMesh::RxEntry rx[RX_LOG_SIZE];
  int n = easy_mesh.rxLog(rx, max < RX_LOG_SIZE ? max : RX_LOG_SIZE);
  uint32_t now = millis();
  for (int i = 0; i < n; i++) {
    PacketView& p = out[i];
    p.ago_s = (now - rx[i].at_ms) / 1000;
    p.snr = rx[i].snr;
    p.rssi = rx[i].rssi;
    p.len = rx[i].len;
    uint8_t route = rx[i].header & PH_ROUTE_MASK;
    p.type = (rx[i].header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;
    p.flood = route == ROUTE_TYPE_FLOOD || route == ROUTE_TYPE_TRANSPORT_FLOOD;
    p.scoped = route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT;
    p.hops = rx[i].path_len & 0x3F;
  }
  return n;
}

bool DeviceModel::discover() { return easy_mesh.discover(); }

int DeviceModel::discovered(DiscoveredView* out, int max) {
  EasyMesh::Discovered d[DISC_MAX];
  int n = easy_mesh.discovered(d, max < DISC_MAX ? max : DISC_MAX);
  for (int i = 0; i < n; i++) {
    DiscoveredView& v = out[i];
    memset(&v, 0, sizeof(v));
    memcpy(v.key, d[i].prefix, LOG_KEY_LEN);
    v.type = d[i].type;
    v.snr_them = d[i].snr_them;
    v.snr_us = d[i].snr_us;
    ContactInfo* c = easy_mesh.lookupContactByPubKey(d[i].prefix, 8);
    if (c) strncpy(v.name, c->name, sizeof(v.name) - 1);
  }
  return n;
}

// ---------------------------------------------------------------- identity & settings

const char* DeviceModel::nodeName() { return easy_mesh.getNodeName(); }

void DeviceModel::setNodeName(const char* name) {
  NodePrefs* p = easy_mesh.getNodePrefs();
  strncpy(p->node_name, name, sizeof(p->node_name) - 1);
  p->node_name[sizeof(p->node_name) - 1] = 0;
  easy_mesh.savePrefs();
}

void DeviceModel::nodeId(char* out, int n) {
  const uint8_t* k = easy_mesh.self_id.pub_key;
  snprintf(out, n, "%02X%02X%02X%02X", k[0], k[1], k[2], k[3]);
}

RadioSettings DeviceModel::radio() {
  NodePrefs* p = easy_mesh.getNodePrefs();
  RadioSettings r = { p->freq, p->bw, p->sf, p->cr, p->tx_power_dbm };
  return r;
}

void DeviceModel::setRadio(const RadioPreset& rp) {
  NodePrefs* p = easy_mesh.getNodePrefs();
  p->freq = rp.freq; p->bw = rp.bw; p->sf = rp.sf; p->cr = rp.cr;
  easy_mesh.savePrefs();
  radio_driver.setParams(p->freq, p->bw, p->sf, p->cr);
}

const char* DeviceModel::regionScope() { return easy_mesh.getNodePrefs()->default_scope_name; }
void DeviceModel::setRegionScope(const char* scope) { easy_mesh.setDefaultScope(scope); }

int DeviceModel::tzSetting() { return easy_mesh.getNodePrefs()->tz_offset; }
int DeviceModel::tzOffset() { return tzOffsetAt(tzSetting(), rtc_clock.getCurrentTime()); }
void DeviceModel::setTzOffset(int h) {
  if (h != TZ_AU_EAST && (h < -12 || h > 14)) return;
  easy_mesh.getNodePrefs()->tz_offset = h;
  easy_mesh.savePrefs();
}

void DeviceModel::setRadioCustom(const RadioSettings& r) {
  if (r.freq < 137 || r.freq > 1020 || r.bw < 7 || r.bw > 500 || r.sf < 5 || r.sf > 12 || r.cr < 5 || r.cr > 8) return;
  NodePrefs* p = easy_mesh.getNodePrefs();
  p->freq = r.freq; p->bw = r.bw; p->sf = r.sf; p->cr = r.cr;
  if (r.tx_power >= -9 && r.tx_power <= 22) p->tx_power_dbm = r.tx_power;   // SX1262 range
  easy_mesh.savePrefs();
  radio_driver.setParams(p->freq, p->bw, p->sf, p->cr);
  radio_driver.setTxPower(p->tx_power_dbm);
}

int DeviceModel::pathHashBytes() { return easy_mesh.getNodePrefs()->path_hash_mode + 1; }
void DeviceModel::setPathHashBytes(int bytes) {
  if (bytes < 1 || bytes > 3) return;
  easy_mesh.getNodePrefs()->path_hash_mode = bytes - 1;   // MeshCore: 0 = 1 byte ... 2 = 3 bytes
  easy_mesh.savePrefs();
}

void DeviceModel::setTime(uint32_t t) { rtc_clock.setCurrentTime(t); _clock_trusted = true; s_clock_trusted = CLOCK_TRUST_MAGIC; _clock_src = "manual"; }

// Better clock: internet time whenever Wi-Fi was on (most accurate, always taken), otherwise the
// mesh (repeater adverts) while nothing else has set the clock yet.
const char* DeviceModel::loopClock() {
  static uint32_t next = 0;
  uint32_t ms = millis();
  if ((int32_t)(ms - next) < 0) return nullptr;
  next = ms + 10000;
  uint32_t t;
  if (netTimeTake(t)) {
    rtc_clock.setCurrentTime(t); _clock_trusted = true; s_clock_trusted = CLOCK_TRUST_MAGIC; _clock_src = "internet";
    return nullptr;   // quiet: it only corrects the clock
  }
  int n = 0;
  if (!clockTrusted() && easy_mesh.meshTime(t, n)) {
    rtc_clock.setCurrentTime(t); _clock_trusted = true; s_clock_trusted = CLOCK_TRUST_MAGIC; _clock_src = "mesh";
    static char b[48];
    snprintf(b, sizeof(b), "Clock set from the mesh (%d repeaters)", n);
    return b;
  }
  return nullptr;
}

// MeshCore seeds the clock with a fixed date (or a backup from before a reset), so "a plausible
// date" says nothing. The clock is trusted once somebody set it: the user, GPS, or the phone
// (CMD_SET_DEVICE_TIME). Those all show up as a jump against our own uptime count.
bool DeviceModel::clockTrusted() {
  if (_clock_trusted) return true;
  uint32_t now = rtc_clock.getCurrentTime();
  uint32_t expect = _clk_ref_rtc + (millis() - _clk_ref_ms) / 1000;
  int32_t diff = (int32_t)(now - expect);
  if (diff > 30 || diff < -30) { _clock_trusted = true; s_clock_trusted = CLOCK_TRUST_MAGIC; _clock_src = "GPS/phone"; }
  return _clock_trusted;
}

uint32_t DeviceModel::blePin() { return easy_mesh.getBLEPin(); }

void DeviceModel::setBluetooth(bool on) {
  _bt_on = on;
  if (on) _serial->enableBluetooth(); else _serial->disableBluetooth();
  if (_ui.bt_off != !on) { _ui.bt_off = !on; saveUi(); }   // remembered across reboots
}

void DeviceModel::setGps(bool on) {
  NodePrefs* p = easy_mesh.getNodePrefs();
  p->gps_enabled = on ? 1 : 0;
  if (on) board.switchGpsBaud();   // fast (<100 ms), leaves Serial1 open at 38400
  sensors.setSettingValue("gps", on ? "1" : "0");
  easy_mesh.savePrefs();
}

bool DeviceModel::advertLocation() { return easy_mesh.getNodePrefs()->advert_loc_policy != ADVERT_LOC_NONE; }
void DeviceModel::setAdvertLocation(bool on) {
  easy_mesh.getNodePrefs()->advert_loc_policy = on ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  easy_mesh.savePrefs();
}

DeviceStatus DeviceModel::status() {
  DeviceStatus s = {};
  uint32_t ms = millis();
  if (_batt_at == 0 || ms - _batt_at > 10000) {   // ADC reads are slow-ish: cache for 10 s
    _batt_at = ms ? ms : 1;
    int raw = board.getBattMilliVolts();
    // Wi-Fi transfers pull the cell voltage down by a few hundred mV, and the percentage comes
    // from the voltage: skip those samples, and smooth the rest (~1 min) so the battery doesn't
    // appear to drop 10 % in a minute and come back.
    bool loaded = tile_downloader.busy() || updater.busy();
    if (!_batt_smooth) _batt_smooth = raw;
    else if (!loaded) _batt_smooth += (raw - _batt_smooth) / 5;
    _batt_mv = raw;
    _batt_pct = TDeckPlusBoard::percentFromMv(_batt_smooth);
  }
  s.battery_mv = _batt_mv;
  s.battery_pct = _batt_pct;
  s.charging = _batt_mv > 4250 || HWCDC::isPlugged();   // charger (voltage) or a computer on USB
  s.bt_enabled = _bt_on;
  s.bt_connected = _ble ? _ble->isConnected() : _serial->isConnected();
  s.gps_enabled = easy_mesh.getNodePrefs()->gps_enabled;
  s.lat = sensors.node_lat;
  s.lon = sensors.node_lon;
  s.gps_fix = s.gps_enabled && (s.lat != 0 || s.lon != 0);
  LocationProvider* lp = sensors.getLocationProvider();
  s.gps_sats = lp ? (int)lp->satellitesCount() : 0;
  s.sd_card = sdPresent();
  s.under_launcher = board.isRunningUnderLauncher();
  s.now = rtc_clock.getCurrentTime();
  s.clock_valid = clockTrusted();
  s.free_heap_kb = ESP.getFreeHeap() / 1024;
  s.free_psram_kb = ESP.getFreePsram() / 1024;
  s.contacts = easy_mesh.getNumContacts();
  s.messages_stored = _log->nextSeq() - _log->firstSeq();
  return s;
}

// ---------------------------------------------------------------- hardware

void DeviceModel::setBacklight(uint8_t level) {
  // Power saving: with the screen off the CPU only has to run the radio, BLE and input
  // polling, so drop to 80 MHz (APB stays 80 MHz, so SPI/I2C/UART timing is unaffected).
  static uint32_t cur_mhz = 240;
  uint32_t want = level == 0 ? 80 : (_ui.battery_saver >= 2 ? 160 : 240);   // maximum saving: a bit slower UI
  if (want != cur_mhz) { setCpuFrequencyMhz(want); cur_mhz = want; }
  tft.setBacklight(level);
}
void DeviceModel::setKeyboardLight(bool on) { input.setKeyboardBacklight(on && _ui.kb_light ? 80 : 0); }

void DeviceModel::playSound(SoundKind k) {
  if (k == SOUND_SOS_ALARM) { sound.play(TONE_ALARM); return; }   // incoming SOS: always audible, even with sounds off
  if (!_ui.sound) return;
  if (k == SOUND_ALERT) { sound.play(TONE_CHIME); return; }
  int msg_tone = _ui.tone % NUM_TONES;   // TONE_CHIME / TONE_BEEP / TONE_PING
  if (k == SOUND_MESSAGE) sound.play(msg_tone);
  else if (k == SOUND_GROUP) {
    int g = _ui.group_tone % NUM_GROUP_TONES;
    if (g == 0) sound.play(msg_tone); else if (g == 1) sound.play(TONE_TICK);
  }
  else sound.click();
}

// Keyboard-light blink pattern for new messages while the screen is off (no LED on the T-Deck)
static uint32_t flash_until = 0;
void DeviceModel::notifyFlash() { flash_until = millis() + 2400; }
bool DeviceModel::flashing() const { return flash_until != 0; }
void DeviceModel::loopFlash() {
  if (!flash_until) return;
  uint32_t now = millis();
  if ((int32_t)(now - flash_until) >= 0) { flash_until = 0; input.setKeyboardBacklight(_ui.kb_light ? 80 : 0); return; }
  bool on = ((flash_until - now) / 300) % 2;
  if (on != _flash_on) { _flash_on = on; input.setKeyboardBacklight(on ? 255 : 0); }   // I2C only on change
}

void DeviceModel::rebootToLauncher() { board.rebootToLauncher(); }

void DeviceModel::factoryReset() {
  storageFactoryReset();
  delay(300);
  ESP.restart();
}

const char* DeviceModel::diagnostics() {
  static String d;
  d = storageDiagnostics();
  return d.c_str();
}

uint32_t DeviceModel::inputEdges(int dir) { return input.edgeCount(dir); }

const char* DeviceModel::lastResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "watchdog (frozen)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "low voltage (brownout)";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_DEEPSLEEP: return "wake from sleep";
    default: return "";
  }
}

bool DeviceModel::crashedLastBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

// ---------------------------------------------------------------- GPS track (binary file on SD/flash)
#define TRACK_FILE "/easydeck_track.bin"

#define TRACK_KEEP 1500   // points kept after compaction (~4 h at one point per 10 s)

void DeviceModel::trackAppend(const TrackPoint& p) {
  File f = DataFS.open(TRACK_FILE, FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&p, sizeof(p));
  size_t sz = f.size();
  f.close();
  // Never let the track fill the data partition (contacts/prefs share it): once it reaches
  // 2x TRACK_KEEP, rewrite it with the newest TRACK_KEEP points.
  if (sz < 2 * TRACK_KEEP * sizeof(TrackPoint)) return;
  TrackPoint* buf = (TrackPoint*)ps_malloc(TRACK_KEEP * sizeof(TrackPoint));
  if (!buf) return;
  int n = trackLoad(buf, TRACK_KEEP);
  File w = DataFS.open(TRACK_FILE, "w");
  if (w) { w.write((const uint8_t*)buf, n * sizeof(TrackPoint)); w.close(); }
  free(buf);
}

int DeviceModel::trackLoad(TrackPoint* out, int max) {
  File f = DataFS.open(TRACK_FILE, "r");
  if (!f) return 0;
  int total = f.size() / sizeof(TrackPoint);
  int skip = total > max ? total - max : 0;   // keep the most recent points
  f.seek(skip * sizeof(TrackPoint));
  int n = 0;
  while (n < max && f.read((uint8_t*)&out[n], sizeof(TrackPoint)) == sizeof(TrackPoint)) n++;
  f.close();
  return n;
}

void DeviceModel::trackClear() { DataFS.remove(TRACK_FILE); }

// ---------------------------------------------------------------- repeater / room admin

bool DeviceModel::adminLogin(const uint8_t* key, const char* password) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return false;
  if (!isAdmKey(key)) { _cli_n = 0; _status.valid = false; }
  memcpy(_adm_key, key, LOG_KEY_LEN);
  uint32_t est = 0;
  strncpy(_adm_pw, password ? password : "", sizeof(_adm_pw) - 1);
  _adm_pw[sizeof(_adm_pw) - 1] = 0;
  if (easy_mesh.sendLogin(*c, password ? password : "", est) == MSG_SEND_FAILED) { _adm_state = ADM_FAILED; return false; }
  _adm_state = ADM_PENDING;
  _adm_ms = millis();
  return true;
}

AdminState DeviceModel::adminState(const uint8_t* key) {
  if (!isAdmKey(key)) return ADM_NONE;
  if (_adm_state == ADM_PENDING && millis() - _adm_ms > 30000) _adm_state = ADM_FAILED;   // no reply
  return _adm_state;
}

bool DeviceModel::requestStatus(const uint8_t* key) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return false;
  uint32_t est = 0;
  if (!isAdmKey(key)) { memcpy(_adm_key, key, LOG_KEY_LEN); _cli_n = 0; _status.valid = false; _adm_state = ADM_NONE; }
  return easy_mesh.sendRequest(*c, (uint8_t)0x01 /*GET_STATUS*/, _status_tag, est) != MSG_SEND_FAILED;
}

bool DeviceModel::getStatus(const uint8_t* key, RepeaterStatus& out) {
  if (!isAdmKey(key) || !_status.valid) return false;
  out = _status;
  return true;
}

void DeviceModel::cliPush(bool out, const char* t) {
  if (_cli_n == CLI_LINES) { memmove(&_cli[0], &_cli[1], sizeof(CliLine) * (CLI_LINES - 1)); _cli_n--; }
  _cli[_cli_n].out = out;
  strncpy(_cli[_cli_n].text, t, LOG_TEXT_MAX);
  _cli[_cli_n].text[LOG_TEXT_MAX] = 0;
  _cli_n++;
}

bool DeviceModel::sendCli(const uint8_t* key, const char* cmd) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c) return false;
  if (!isAdmKey(key)) { memcpy(_adm_key, key, LOG_KEY_LEN); _cli_n = 0; }
  uint32_t est = 0;
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  bool ok = easy_mesh.sendCommandData(*c, ts, 0, 1 /*TXT_TYPE_CLI_DATA*/, cmd, est) != MSG_SEND_FAILED;
  cliPush(true, cmd);
  if (!ok) cliPush(false, "(send failed)");
  return ok;
}

int DeviceModel::cliLines(const uint8_t* key, CliLine* out, int max) {
  if (!isAdmKey(key)) return 0;
  int n = _cli_n < max ? _cli_n : max;
  int start = _cli_n - n;
  for (int i = 0; i < n; i++) out[i] = _cli[start + i];
  return n;
}

void DeviceModel::onCliReply(const uint8_t* pubkey, const char* text) {
  if (isAdmKey(pubkey)) cliPush(false, text);
}

bool DeviceModel::onResponse(const uint8_t* pubkey, uint32_t tag, const uint8_t* d, uint8_t len) {
  // login reply: [0]=0 OK (else fail), [1] legacy, [2] is_admin, [3] permissions ...
  // (status replies are matched by tag, so they can't be mistaken for the login reply)
  bool legacy_ok = len >= 2 && d[0] == 'O' && d[1] == 'K';   // old repeater firmware
  if (_adm_state == ADM_PENDING && isAdmKey(pubkey) && len >= 1 && !(tag == _status_tag && _status_tag)) {
    if (d[0] == 0 || legacy_ok) {   // RESP_SERVER_LOGIN_OK: [1] keep-alive/16, [2] permissions
      _adm_state = (!legacy_ok && len >= 3 && (d[2] & 1)) ? ADM_ADMIN : ADM_GUEST;
      ContactInfo* c = easy_mesh.contactByKey(pubkey);
      if (c && c->type == 3) rememberRoom(pubkey, _adm_pw);   // re-join this room after reboots
    } else _adm_state = ADM_FAILED;
    return true;
  }
  // GET_STATUS reply: RepeaterStats (52 bytes, little-endian, packed)
  if (tag == _status_tag && _status_tag && len >= 52 && isAdmKey(pubkey)) {
    RepeaterStatus& s = _status;
    auto u16 = [&](int o) { uint16_t v; memcpy(&v, &d[o], 2); return v; };
    auto i16 = [&](int o) { int16_t v; memcpy(&v, &d[o], 2); return v; };
    auto u32 = [&](int o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    s.batt_mv = u16(0); s.tx_queue = u16(2); s.noise_floor = i16(4); s.last_rssi = i16(6);
    s.pkts_recv = u32(8); s.pkts_sent = u32(12); s.air_secs = u32(16); s.uptime_secs = u32(20);
    s.sent_flood = u32(24); s.sent_direct = u32(28); s.recv_flood = u32(32); s.recv_direct = u32(36);
    s.err_events = u16(40); s.last_snr = i16(42) / 4.0f; s.direct_dups = u16(44); s.flood_dups = u16(46);
    s.at = rtc_clock.getCurrentTime();
    s.valid = true;
    _status_tag = 0;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- SD text files
bool DeviceModel::readTextFile(const char* path, char* buf, int max) {
  if (!sdPresent()) return false;
  File f = SD.open(path, "r");
  if (!f) return false;
  int n = f.read((uint8_t*)buf, max - 1);
  f.close();
  buf[n < 0 ? 0 : n] = 0;
  return true;
}

bool DeviceModel::writeTextFile(const char* path, const char* text) {
  if (!sdPresent()) return false;
  File f = SD.open(path, "w");
  if (!f) return false;
  f.print(text);
  f.close();
  return true;
}

bool DeviceModel::writeFile(const char* path, const uint8_t* data, size_t len) {
  if (!sdPresent()) return false;
  const char* slash = strrchr(path, '/');
  if (slash && slash != path) {   // make the folder
    char dir[64];
    size_t n = (size_t)(slash - path) < sizeof(dir) - 1 ? (size_t)(slash - path) : sizeof(dir) - 1;
    memcpy(dir, path, n); dir[n] = 0;
    SD.mkdir(dir);
  }
  File f = SD.open(path, "w");
  if (!f) return false;
  size_t done = 0;
  while (done < len) {   // chunks, so the loop watchdog can be fed on a slow card
    size_t c = len - done > 16384 ? 16384 : len - done;
    if (f.write(data + done, c) != c) break;
    done += c;
    feedLoopWDT();
  }
  f.close();
  return done == len;
}

bool DeviceModel::fileExists(const char* path) { return sdPresent() && SD.exists(path); }

bool DeviceModel::renameFile(const char* from, const char* to) {
  if (!sdPresent()) return false;
  SD.remove(to);
  return SD.rename(from, to);
}

// ---------------------------------------------------------------- self test
int DeviceModel::selfTest(SelfTestItem* out, int max) {
  int n = 0;
  auto add = [&](const char* name, TestResult r, const char* fmt, ...) {
    if (n >= max) return;
    SelfTestItem& it = out[n++];
    it.name = name;
    it.result = r;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it.detail, sizeof(it.detail), fmt, ap);
    va_end(ap);
  };
  DeviceStatus st = status();
  RadioStats rs = radioStats();
  add("Radio", TEST_PASS, "SX1262 ok, RX %lu TX %lu", (unsigned long)rs.rx, (unsigned long)rs.tx);   // we only get here if init succeeded
  add("Keyboard", input.hasKeyboard() ? TEST_PASS : TEST_FAIL, input.hasKeyboard() ? "found at 0x55" : "not found on I2C");
  add("Touch", input.hasTouch() ? TEST_PASS : TEST_WARN, input.hasTouch() ? "GT911 found" : "not found (trackball still works)");
  add("SD card", st.sd_card ? TEST_PASS : TEST_WARN, st.sd_card ? "mounted" : "none - history kept in flash");
  {   // how close the UI (loop task) came to running out of stack since boot
    char d[48];
    UBaseType_t free_b = uxTaskGetStackHighWaterMark(NULL);
    snprintf(d, sizeof(d), "%u bytes spare (lowest)", (unsigned)free_b);
    add("UI stack", free_b < 2048 ? TEST_WARN : TEST_PASS, d);
  }
  add("Storage", storageMode() == STORAGE_NONE ? TEST_FAIL : TEST_PASS, storageMode() == STORAGE_FLASH ? "own flash partition" : "SD /easydeck");
  if (!st.gps_enabled) add("GPS", TEST_WARN, "turned off");
  else if (st.gps_fix) add("GPS", TEST_PASS, "fix, %d sats", st.gps_sats);
  else if (board.gpsBaudFound()) add("GPS", TEST_WARN, "found @%lu, no fix yet (%d sats)", (unsigned long)board.gpsBaudFound(), st.gps_sats);
  else add("GPS", st.gps_sats ? TEST_WARN : TEST_FAIL, st.gps_sats ? "searching, %d sats" : "no data from receiver", st.gps_sats);
  bool batt_ok = st.battery_mv > 3000 && st.battery_mv < 4500;
  add("Battery", batt_ok ? (st.battery_pct < 15 ? TEST_WARN : TEST_PASS) : TEST_FAIL, "%.2f V (%d%%)", st.battery_mv / 1000.0, st.battery_pct);
  add("Sound", sound.ok() ? TEST_PASS : TEST_FAIL, sound.ok() ? "I2S ready" : "I2S init failed");
  add("Bluetooth", st.bt_enabled ? TEST_PASS : TEST_WARN, st.bt_connected ? "phone connected" : (st.bt_enabled ? "advertising" : "off"));
  add("Memory", st.free_psram_kb > 1000 && st.free_heap_kb > 30 ? TEST_PASS : TEST_WARN, "RAM %lu KB, PSRAM %lu KB free", (unsigned long)st.free_heap_kb, (unsigned long)st.free_psram_kb);
  add("Clock", st.clock_valid ? TEST_PASS : TEST_WARN, st.clock_valid ? "set" : "not set (GPS/phone/Settings)");
  return n;
}

// ---------------------------------------------------------------- remembered rooms
// Room servers only push posts to clients that logged in recently, so we re-join the rooms
// the user joined before, one every 20 s once the clock is set.
#define ROOMS_FILE "/easydeck_rooms.bin"

void DeviceModel::saveRooms() {
  File f = DataFS.open(ROOMS_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)&_n_rooms, sizeof(_n_rooms));
  f.write((const uint8_t*)_rooms, sizeof(RoomLogin) * _n_rooms);
  f.close();
}

void DeviceModel::rememberRoom(const uint8_t* key, const char* pw) {
  int i = 0;
  for (; i < _n_rooms; i++) if (memcmp(_rooms[i].key, key, LOG_KEY_LEN) == 0) break;
  if (i == _n_rooms) {
    if (_n_rooms == MAX_ROOMS) { memmove(&_rooms[0], &_rooms[1], sizeof(RoomLogin) * (MAX_ROOMS - 1)); i = MAX_ROOMS - 1; }
    else _n_rooms++;
  }
  memcpy(_rooms[i].key, key, LOG_KEY_LEN);
  strncpy(_rooms[i].pw, pw, sizeof(_rooms[i].pw) - 1);
  _rooms[i].pw[sizeof(_rooms[i].pw) - 1] = 0;
  saveRooms();
}

void DeviceModel::loopRooms() {
  uint32_t now = millis();
  if (!_rooms_loaded) {
    _rooms_loaded = true;
    File f = DataFS.open(ROOMS_FILE, "r");
    if (f) {
      int n = 0;
      if (f.read((uint8_t*)&n, sizeof(n)) == sizeof(n) && n > 0 && n <= MAX_ROOMS &&
          f.read((uint8_t*)_rooms, sizeof(RoomLogin) * n) == (int)(sizeof(RoomLogin) * n)) _n_rooms = n;
      f.close();
    }
    _room_ms = now + 45000;   // give the mesh / clock a moment after boot
    return;
  }
  if (_room_next >= _n_rooms || (int32_t)(now - _room_ms) < 0) return;
  if (!clockTrusted()) { _room_ms = now + 30000; return; }
  if (_adm_state == ADM_PENDING) { _room_ms = now + 5000; return; }   // a manual login is in flight
  RoomLogin& r = _rooms[_room_next++];
  ContactInfo* c = easy_mesh.contactByKey(r.key);
  if (c) {
    uint32_t est = 0;
    memcpy(_adm_key, r.key, LOG_KEY_LEN);
    strncpy(_adm_pw, r.pw, sizeof(_adm_pw));
    if (easy_mesh.sendLogin(*c, r.pw, est) != MSG_SEND_FAILED) { _adm_state = ADM_PENDING; _adm_ms = now; }
  }
  _room_ms = now + 20000;
}

// ---------------------------------------------------------------- Wi-Fi + offline maps
bool DeviceModel::wifiScanStart() { return tile_downloader.scanStart(); }
int DeviceModel::wifiScanResults(WifiNet* out, int max) { return tile_downloader.scanResults(out, max); }
void DeviceModel::wifiScanStop() { tile_downloader.scanStop(); }
void DeviceModel::setWifi(const char* ssid, const char* pass) { tile_downloader.setWifi(ssid, pass); }
const char* DeviceModel::wifiSsid() { return tile_downloader.ssid(); }
void DeviceModel::setTileUrl(const char* url) { tile_downloader.setUrl(url); }
const char* DeviceModel::tileUrl() { return tile_downloader.url(); }
bool DeviceModel::hasSd() { return sdPresent(); }
bool DeviceModel::tileDownloadStart(double lat, double lon, double r, int zmin, int zmax) { return tile_downloader.start(lat, lon, r, zmin, zmax); }
void DeviceModel::tileDownloadCancel() { tile_downloader.cancel(); }
TileDlStatus DeviceModel::tileDownloadStatus() { return tile_downloader.status(); }

void DeviceModel::gpsDetail(GpsDetail& out) {
  gps_tap.detail(out);
  out.enabled = easy_mesh.getNodePrefs()->gps_enabled;
}

// ---------------------------------------------------------------- waypoints
#define WP_FILE  "/easydeck_wp"
#define WP_MAGIC 0x57505431UL   // "WPT1"

void DeviceModel::loadWaypoints() {
  if (_wp) return;
  _wp = (WpFile*)calloc(1, sizeof(WpFile));
  if (!_wp) return;
  if (!safeLoad(DataFS, WP_FILE, WP_MAGIC, _wp, sizeof(WpFile)) || _wp->n < 0 || _wp->n > MAX_WAYPOINTS) _wp->n = 0;
  for (int i = 0; i < _wp->n; i++) _wp->w[i].name[sizeof(_wp->w[i].name) - 1] = 0;
}
int DeviceModel::waypointCount() { loadWaypoints(); return _wp ? _wp->n : 0; }
bool DeviceModel::waypointAt(int i, Waypoint& out) {
  loadWaypoints();
  if (!_wp || i < 0 || i >= _wp->n) return false;
  out = _wp->w[i];
  return true;
}
bool DeviceModel::addWaypoint(const Waypoint& w) {
  loadWaypoints();
  if (!_wp || _wp->n >= MAX_WAYPOINTS) return false;
  _wp->w[_wp->n] = w;
  _wp->w[_wp->n].name[sizeof(w.name) - 1] = 0;
  _wp->n++;
  return safeSave(DataFS, WP_FILE, WP_MAGIC, _wp, sizeof(WpFile));
}
void DeviceModel::removeWaypoint(int i) {
  loadWaypoints();
  if (!_wp || i < 0 || i >= _wp->n) return;
  memmove(&_wp->w[i], &_wp->w[i + 1], sizeof(Waypoint) * (_wp->n - i - 1));
  _wp->n--;
  safeSave(DataFS, WP_FILE, WP_MAGIC, _wp, sizeof(WpFile));
}

void DeviceModel::advertEcho(bool& active, uint32_t& age_s, int& count) {
  EasyMesh::EchoInfo e = easy_mesh.advertEcho();
  active = e.active; age_s = e.age_s; count = e.count;
}

// ---------------------------------------------------------------- muted groups
#define MUTE_FILE  "/easydeck_mute"
#define MUTE_MAGIC 0x4D555431UL   // "MUT1"

void DeviceModel::loadMutes() {
  if (_mute) return;
  _mute = (MuteFile*)calloc(1, sizeof(MuteFile));
  if (_mute && (!safeLoad(DataFS, MUTE_FILE, MUTE_MAGIC, _mute, sizeof(MuteFile)) || _mute->n < 0 || _mute->n > MAX_MUTED)) _mute->n = 0;
}
bool DeviceModel::channelMuted(const uint8_t* key) {
  loadMutes();
  if (!_mute) return false;
  for (int i = 0; i < _mute->n; i++) if (memcmp(_mute->key[i], key, LOG_KEY_LEN) == 0) return true;
  return false;
}
bool DeviceModel::setChannelMuted(const uint8_t* key, bool muted) {
  loadMutes();
  if (!_mute) return false;
  if (channelMuted(key) == muted) return true;
  if (muted) {
    if (_mute->n >= MAX_MUTED) return false;
    memcpy(_mute->key[_mute->n++], key, LOG_KEY_LEN);
  } else {
    for (int i = 0; i < _mute->n; i++)
      if (memcmp(_mute->key[i], key, LOG_KEY_LEN) == 0) { memmove(_mute->key[i], _mute->key[i + 1], LOG_KEY_LEN * (_mute->n - i - 1)); _mute->n--; break; }
  }
  safeSave(DataFS, MUTE_FILE, MUTE_MAGIC, _mute, sizeof(MuteFile));
  return true;
}

// ---------------------------------------------------------------- contact notes
#define NOTES_FILE  "/easydeck_notes"
#define NOTES_MAGIC 0x4E4F5431UL   // "NOT1"

void DeviceModel::loadNotes() {
  if (_notes) return;
  _notes = (NoteFile*)ps_calloc(1, sizeof(NoteFile));   // PSRAM: keep internal RAM free
  if (!_notes) return;
  if (!safeLoad(DataFS, NOTES_FILE, NOTES_MAGIC, _notes, sizeof(NoteFile)) || _notes->n < 0 || _notes->n > MAX_NOTES) _notes->n = 0;
  for (int i = 0; i < _notes->n; i++) _notes->e[i].text[CONTACT_NOTE_MAX] = 0;
}
void DeviceModel::contactNote(const uint8_t* key, char* out) {
  out[0] = 0;
  loadNotes();
  if (!_notes) return;
  for (int i = 0; i < _notes->n; i++)
    if (memcmp(_notes->e[i].key, key, LOG_KEY_LEN) == 0) { strncpy(out, _notes->e[i].text, CONTACT_NOTE_MAX); out[CONTACT_NOTE_MAX] = 0; return; }
}
bool DeviceModel::setContactNote(const uint8_t* key, const char* note) {
  loadNotes();
  if (!_notes) return false;
  int i = 0;
  for (; i < _notes->n; i++) if (memcmp(_notes->e[i].key, key, LOG_KEY_LEN) == 0) break;
  if (!note || !*note) {   // clear
    if (i < _notes->n) { _notes->e[i] = _notes->e[--_notes->n]; safeSave(DataFS, NOTES_FILE, NOTES_MAGIC, _notes, sizeof(NoteFile)); }
    return true;
  }
  if (i == _notes->n) { if (_notes->n >= MAX_NOTES) return false; _notes->n++; }
  memcpy(_notes->e[i].key, key, LOG_KEY_LEN);
  strncpy(_notes->e[i].text, note, CONTACT_NOTE_MAX);
  _notes->e[i].text[CONTACT_NOTE_MAX] = 0;
  return safeSave(DataFS, NOTES_FILE, NOTES_MAGIC, _notes, sizeof(NoteFile));
}

// ---------------------------------------------------------------- route view
// out_path_len uses MeshCore's path encoding: low 6 bits = hop count, top 2 bits = hash size - 1.
static void nameHop(PathHop& p) {   // contacts whose public key starts with this hash (repeaters preferred)
  ContactInfo ci;
  for (int i = MAX_ANON_CONTACTS; easy_mesh.getContactByIdx(i, ci); i++) {
    if (memcmp(ci.id.pub_key, p.hash, p.hash_len) != 0) continue;
    p.matches++;
    if (!p.name[0] || ci.type == ADV_TYPE_REPEATER) strncpy(p.name, ci.name, sizeof(p.name) - 1);
  }
}

int DeviceModel::messageRepeaters(const LogRecord& r, PathHop* out, int max) {
  int n = 0;
  if (r.rpt_hsize < 1 || r.rpt_hsize > 3) return 0;               // stored data: never trust sizes
  int cnt = r.rpt_n;
  if (cnt * r.rpt_hsize > (int)sizeof(r.rpt)) cnt = sizeof(r.rpt) / r.rpt_hsize;
  for (int i = 0; i < cnt && n < max; i++) {
    PathHop& p = out[n++];
    memset(&p, 0, sizeof(p));
    p.hash_len = r.rpt_hsize;
    memcpy(p.hash, &r.rpt[i * r.rpt_hsize], r.rpt_hsize);
    nameHop(p);
  }
  return n;
}

int DeviceModel::contactPath(const uint8_t* key, PathHop* out, int max) {
  ContactInfo* c = easy_mesh.contactByKey(key);
  if (!c || c->out_path_len == OUT_PATH_UNKNOWN) return -1;
  int hops = c->out_path_len & 0x3F, sz = (c->out_path_len >> 6) + 1;
  if (hops * sz > MAX_PATH_SIZE) return -1;
  int n = 0;
  for (int h = 0; h < hops && n < max; h++) {
    PathHop& p = out[n++];
    memset(&p, 0, sizeof(p));
    p.hash_len = (uint8_t)(sz > 4 ? 4 : sz);
    memcpy(p.hash, &c->out_path[h * sz], p.hash_len);
    // name it: contacts whose public key starts with this hash (repeaters preferred)
    ContactInfo ci;
    for (int i = MAX_ANON_CONTACTS; easy_mesh.getContactByIdx(i, ci); i++) {
      if (memcmp(ci.id.pub_key, p.hash, p.hash_len) != 0) continue;
      p.matches++;
      if (!p.name[0] || ci.type == ADV_TYPE_REPEATER) strncpy(p.name, ci.name, sizeof(p.name) - 1);
    }
  }
  return n;
}

// ---------------------------------------------------------------- owner / emergency info
#define OWNER_FILE  "/easydeck_owner"
#define OWNER_MAGIC 0x4F574E31UL   // "OWN1"
void DeviceModel::ownerInfo(OwnerInfo& out) {
  memset(&out, 0, sizeof(out));
  if (safeLoad(DataFS, OWNER_FILE, OWNER_MAGIC, &out, sizeof(out))) {
    out.owner[sizeof(out.owner) - 1] = 0; out.ice[sizeof(out.ice) - 1] = 0; out.medical[sizeof(out.medical) - 1] = 0;
  } else memset(&out, 0, sizeof(out));
}
void DeviceModel::setOwnerInfo(const OwnerInfo& in) { safeSave(DataFS, OWNER_FILE, OWNER_MAGIC, &in, sizeof(in)); }

bool DeviceModel::startTrace(const uint8_t* key) { return easy_mesh.startTrace(key); }
TraceResult DeviceModel::traceResult() { return easy_mesh.traceResult(); }

// ---------------------------------------------------------------- auto-add + add from Heard
#define DISC_KEEP 16
#define AA_OVERWRITE 0x01
#define AA_CHAT      0x02
#define AA_REPEATER  0x04
#define AA_ROOM      0x08
#define AA_SENSOR    0x10

AutoAddCfg DeviceModel::autoAdd() {
  NodePrefs* p = easy_mesh.getNodePrefs();
  AutoAddCfg c;
  c.overwrite = p->autoadd_config & AA_OVERWRITE;
  c.max_hops = p->autoadd_max_hops;
  if (!(p->manual_add_contacts & 1)) c.mode = 0;
  else if (p->autoadd_config & AA_REPEATER) c.mode = 2;
  else if (p->autoadd_config & AA_CHAT) c.mode = 1;
  else c.mode = 3;
  return c;
}

void DeviceModel::setAutoAdd(const AutoAddCfg& c) {
  NodePrefs* p = easy_mesh.getNodePrefs();
  uint8_t cfg = c.overwrite ? AA_OVERWRITE : 0;
  if (c.mode == 1) cfg |= AA_CHAT;
  else if (c.mode == 2) cfg |= AA_CHAT | AA_REPEATER | AA_ROOM;
  p->manual_add_contacts = c.mode == 0 ? 0 : 1;
  p->autoadd_config = cfg;
  p->autoadd_max_hops = c.max_hops > 64 ? 64 : c.max_hops;
  easy_mesh.savePrefs();
}

void DeviceModel::noteDiscovered(const ContactInfo& ci) {
  if (!_disc) _disc = (Disc*)ps_calloc(DISC_KEEP, sizeof(Disc));
  if (!_disc) return;
  for (int i = 0; i < DISC_KEEP; i++)   // update in place
    if (_disc[i].used && memcmp(_disc[i].ci.id.pub_key, ci.id.pub_key, PUB_KEY_SIZE) == 0) { _disc[i].ci = ci; return; }
  _disc[_disc_next].ci = ci;
  _disc[_disc_next].used = true;
  _disc_next = (_disc_next + 1) % DISC_KEEP;
}

bool DeviceModel::canAddHeard(const uint8_t* key) {
  if (!_disc || easy_mesh.contactByKey(key)) return false;
  for (int i = 0; i < DISC_KEEP; i++) if (_disc[i].used && memcmp(_disc[i].ci.id.pub_key, key, LOG_KEY_LEN) == 0) return true;
  return false;
}

bool DeviceModel::addHeard(const uint8_t* key) {
  if (!_disc) return false;
  for (int i = 0; i < DISC_KEEP; i++) {
    if (!_disc[i].used || memcmp(_disc[i].ci.id.pub_key, key, LOG_KEY_LEN) != 0) continue;
    if (easy_mesh.contactByKey(key)) { _disc[i].used = false; return true; }   // added meanwhile (advert / phone)
    _disc[i].ci.lastmod = rtc_clock.getCurrentTime();   // so the phone app's incremental sync picks it up
    if (!easy_mesh.addContact(_disc[i].ci)) return false;   // list full
    _disc[i].used = false;
    saveContacts();
    deviceUiContactsChanged();
    return true;
  }
  return false;
}

// ---- telemetry permissions ----
// MeshCore answers a telemetry request only if the BASE permission passes, and adds the position
// if LOCATION passes too, so keep base at least as open as location.
uint8_t DeviceModel::telemetryMode(bool loc) {
  NodePrefs* p = easy_mesh.getNodePrefs();
  uint8_t v = loc ? p->telemetry_mode_loc : p->telemetry_mode_base;
  return v <= TM_ALL ? v : TM_DENY;
}

void DeviceModel::setTelemetryMode(bool loc, uint8_t mode) {
  if (mode > TM_ALL) return;
  NodePrefs* p = easy_mesh.getNodePrefs();
  if (loc) {
    p->telemetry_mode_loc = mode;
    if (p->telemetry_mode_base < mode) p->telemetry_mode_base = mode;
  } else {
    p->telemetry_mode_base = mode;
    if (p->telemetry_mode_loc > mode) p->telemetry_mode_loc = mode;
  }
  easy_mesh.savePrefs();
}

void DeviceModel::backupInfo(char* out, int n) {
  if (storageMode() == STORAGE_SD) { snprintf(out, n, "Not needed - data is on the SD card"); return; }
  if (!sdPresent()) { snprintf(out, n, "No SD card"); return; }
  if (_ui.backup_off) { snprintf(out, n, "Off"); return; }
  uint32_t last = backupLastMs();
  if (!last) { snprintf(out, n, "On - first backup soon"); return; }
  uint32_t mins = (millis() - last) / 60000UL;
  if (mins == 0) snprintf(out, n, "On - saved just now");
  else snprintf(out, n, "On - saved %lu min ago", (unsigned long)mins);
}

bool DeviceModel::updateCheck() { return updater.check(); }
bool DeviceModel::updateDownload() { return updater.download(); }
void DeviceModel::updateCancel() { updater.cancel(); }
UpdateStatus DeviceModel::updateStatus() { return updater.status(); }
void DeviceModel::updateInstall() {
  if (!updater.status().launcher || updater.status().state != UPD_READY) return;
  saveUi();
  board.rebootToLauncher();   // the Launcher shows its menu; pick the file on the SD card there
}

bool DeviceModel::netFetch(const char* url) { return updater.fetch(url); }
int DeviceModel::netFetchState() { return updater.fetchState(); }
const char* DeviceModel::netFetchText() { return updater.fetchText(); }

// ---- Wi-Fi mode ----
bool DeviceModel::takeWifiModeFlag() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool req = (s_wifi_req & 0xFFFFFF00UL) == WIFI_MAGIC && rr == ESP_RST_SW;
  s_wifi_target = req ? (int)(s_wifi_req & 0xFF) : 0;
  s_wifi_req = 0;               // one boot only: any later restart brings Bluetooth back
  s_wifi_boot = req;
  return req;
}

bool DeviceModel::wifiMemoryOk() {
  // Wi-Fi itself (TLS buffers are in PSRAM since 0.51): ~40 KB internal
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= 40000 &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= 16000;
}

void DeviceModel::restartForWifi(int target) {
  s_wifi_req = WIFI_MAGIC | (uint32_t)(target & 0xFF);
  saveUi();
  delay(50);
  esp_restart();
}

void DeviceModel::restartNormal() {
  s_wifi_req = 0;
  saveUi();
  delay(50);
  esp_restart();
}

void DeviceModel::setUseMirror(bool on) {
  if (on) _ui.desk_mode &= ~NO_MIRROR; else _ui.desk_mode |= NO_MIRROR;
  saveUi();
  updater.setMirror(on);
  tile_downloader.setMirror(on);
}

const char* DeviceModel::crashDetails() { return crashSummary(); }
