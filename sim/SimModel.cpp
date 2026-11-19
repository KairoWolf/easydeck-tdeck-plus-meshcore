#include "ui/TextInput.h"
// Fake device for the UI simulator: MidCoast NSW contacts, groups and messages.
#include "SimModel.h"
#include "ui/Sos.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <strings.h>

static void key6(uint8_t* k, uint8_t seed) { for (int i = 0; i < LOG_KEY_LEN; i++) k[i] = (uint8_t)(seed * 37 + i * 11); }

SimModel::SimModel() {
  _now = 1795600000;   // Nov 2026
  struct C { const char* n; uint8_t type; bool fav; int ago; double lat, lon; };
  const C cs[] = {
    { "Mum", 1, true, 300, -32.1830, 152.5170 },
    { "Dad", 1, true, 4000, 0, 0 },
    { "Sarah K", 1, false, 90, -31.8940, 152.4550 },
    { "Jake W", 1, false, 7200, 0, 0 },
    { "Wingham Base", 1, false, 86400 * 2, -31.8690, 152.3740 },
    { "Taree Hill Rptr", 2, false, 60, 0, 0 },
    { "Forster RPT", 2, false, 1800, 0, 0 },
  };
  for (auto& c : cs) {
    ContactView v = {};
    key6(v.key, (uint8_t)(_contacts.size() + 1));
    snprintf(v.name, sizeof(v.name), "%s", c.n);
    v.type = c.type; v.favorite = c.fav; v.last_heard = _now - c.ago;
    v.has_location = c.lat != 0; v.lat = c.lat; v.lon = c.lon;
    v.hops = c.type == 2 ? 0 : 2;
    _contacts.push_back(v);
  }
  const char* chs[] = { "Public", "#test", "#nsw", "#midnorthcoast" };
  for (int i = 0; i < 4; i++) {
    ChannelView ch = {};
    key6(ch.key, (uint8_t)(100 + i));
    snprintf(ch.name, sizeof(ch.name), "%s", chs[i]);
    ch.idx = i;
    _channels.push_back(ch);
  }
  // conversation history
  auto dm = [&](int ci, bool out, int ago, const char* text, uint8_t st = ST_DELIVERED, bool read = true) {
    LogRecord r = {};
    r.kind = KIND_DIRECT; r.dir = out ? DIR_OUT : DIR_IN; r.status = out ? st : ST_NONE;
    memcpy(r.key, _contacts[ci].key, LOG_KEY_LEN);
    snprintf(r.name, sizeof(r.name), "%s", _contacts[ci].name);
    snprintf(r.text, sizeof(r.text), "%s", text);
    r.sender_ts = r.stored_at = _now - ago;
    if (read || out) r.flags |= LOG_FLAG_READ;
    append(r, false);
  };
  auto grp = [&](int gi, const char* from, int ago, const char* text, bool read = true) {
    LogRecord r = {};
    r.kind = KIND_CHANNEL; r.dir = from ? DIR_IN : DIR_OUT; r.status = from ? ST_NONE : ST_SENT;
    memcpy(r.key, _channels[gi].key, LOG_KEY_LEN);
    snprintf(r.name, sizeof(r.name), "%s", from ? from : _name);
    snprintf(r.text, sizeof(r.text), "%s", text);
    r.sender_ts = r.stored_at = _now - ago;
    if (read || !from) r.flags |= LOG_FLAG_READ;
    append(r, false);
  };
  grp(2, "Pete (Port)", 86400 * 2, "Anyone hearing the new Port Macquarie repeater?");
  grp(2, "Lisa", 86400 * 2 - 600, "Yep, strong signal from Laurieton");
  dm(1, false, 86400 + 3600, "Did you get the trailer back?");
  dm(1, true, 86400 + 3000, "Yes, it's in the shed");
  grp(1, "TRO Bot", 7200, "ack @Sarah K | 2 hops | SNR 6.5");
  dm(2, false, 3600, "Heading to Old Bar beach later, keen? \xF0\x9F\x98\x8E\xF0\x9F\x8C\x8A");
  dm(2, true, 3500, "Yes! What time?");
  dm(2, false, 3400, "Around 3. I'll bring the esky \xF0\x9F\x8D\xBA\xF0\x9F\x91\x8D");
  dm(2, true, 600, "Sounds good, see you there", ST_PENDING);
  dm(0, false, 1500, "Are you home for dinner tonight?", ST_NONE, true);
  dm(0, true, 1400, "Yep, be there by 6");
  dm(0, false, 240, "Great, we're having lasagne", ST_NONE, false);
  dm(0, false, 200, "Can you grab milk on the way?", ST_NONE, false);
  grp(3, "Greg", 120, "Storm coming in from the west, take care everyone", false);
}

void SimModel::append(LogRecord& r, bool notify) {
  r.seq = (uint32_t)_log.size();
  r.magic = 1;
  _log.push_back(r);
  if (notify && onAppend) onAppend(r);
}

void SimModel::update(const LogRecord& r) {
  if (r.seq < _log.size()) { _log[r.seq] = r; if (onUpdate) onUpdate(r); }
}

int SimModel::listConversations(ConvSummary* out, int max) {
  int n = 0;
  for (int s = (int)_log.size() - 1; s >= 0; s--) {
    const LogRecord& r = _log[s];
    if (r.flags & LOG_FLAG_SILENT) continue;
    ConvKey k = r.conv();
    int i = 0;
    for (; i < n; i++) if (out[i].key == k) break;
    if (i == n) {
      if (n >= max) continue;
      ConvSummary& c = out[n++];
      memset(&c, 0, sizeof(c));
      c.key = k; c.last_seq = r.seq; c.last_time = r.stored_at; c.last_dir = r.dir; c.last_status = r.status;
      if (r.kind == KIND_CHANNEL && r.dir == DIR_IN) snprintf(c.preview, sizeof(c.preview), "%s: %s", r.name, r.text);
      else snprintf(c.preview, sizeof(c.preview), "%s", r.text);
      snprintf(c.name, sizeof(c.name), "%s", r.name);
    }
    if (r.dir == DIR_IN && !r.isRead()) out[i].unread++;
  }
  return n;
}

int SimModel::conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) {
  int n = 0;
  for (int s = (int)_log.size() - 1; s >= 0 && n < max; s--) if (_log[s].conv() == k) seqs[n++] = s;
  std::reverse(seqs, seqs + n);
  return n;
}

int SimModel::searchMessages(const char* q, uint32_t* seqs, int max) {
  int n = 0;
  for (int s = (int)_log.size() - 1; s >= 0 && n < max; s--)
    if (!(_log[s].flags & LOG_FLAG_SILENT) && (textMatches(_log[s].text, q) || textMatches(_log[s].name, q))) seqs[n++] = s;
  return n;
}

bool SimModel::getMessage(uint32_t seq, LogRecord& out) {
  if (seq >= _log.size()) return false;
  out = _log[seq];
  return true;
}

int SimModel::unreadCount(const ConvKey& k) {
  int n = 0;
  for (auto& r : _log) if (r.conv() == k && r.dir == DIR_IN && !r.isRead()) n++;
  return n;
}
int SimModel::totalUnread() {
  int n = 0;
  for (auto& r : _log) if (r.dir == DIR_IN && !r.isRead()) n++;
  return n;
}
void SimModel::markRead(const ConvKey& k) {
  for (auto& r : _log) if (r.conv() == k && r.dir == DIR_IN && !r.isRead()) { r.flags |= LOG_FLAG_READ; if (onUpdate) onUpdate(r); }
}

bool SimModel::sendText(const ConvKey& k, const char* text) {
  LogRecord r = {};
  r.kind = k.kind; r.dir = DIR_OUT; r.origin = ORIGIN_DEVICE;
  r.status = k.kind == KIND_CHANNEL ? ST_SENT : ST_PENDING;
  r.path_len = k.kind == KIND_CHANNEL ? 0 : 0xFF;
  memcpy(r.key, k.id, LOG_KEY_LEN);
  snprintf(r.text, sizeof(r.text), "%s", text);
  snprintf(r.name, sizeof(r.name), "%s", _name);
  r.sender_ts = r.stored_at = _now;
  r.flags = LOG_FLAG_READ;
  append(r, true);
  _pending_deliver = r.seq;
  return true;
}

void SimModel::deliverPending() {
  if (_pending_deliver < 0) return;
  LogRecord r;
  if (getMessage(_pending_deliver, r) && r.status == ST_PENDING) { r.status = ST_DELIVERED; update(r); }
  else if (getMessage(_pending_deliver, r) && r.kind == KIND_CHANNEL) { r.path_len = 3; update(r); }   // "heard by 3 repeaters"
  _pending_deliver = -1;
}

void SimModel::incoming(int contact, const char* text) {
  LogRecord r = {};
  r.kind = KIND_DIRECT; r.dir = DIR_IN;
  memcpy(r.key, _contacts[contact].key, LOG_KEY_LEN);
  snprintf(r.name, sizeof(r.name), "%s", _contacts[contact].name);
  snprintf(r.text, sizeof(r.text), "%s", text);
  r.sender_ts = r.stored_at = _now;
  if (Sos::isSosText(text)) r.flags |= LOG_FLAG_SOS;
  append(r, true);
}

void SimModel::incomingChannel(int ch, const char* sender, const char* text) {
  if (ch < 0 || ch >= (int)_channels.size()) return;
  LogRecord r = {};
  r.kind = KIND_CHANNEL; r.dir = DIR_IN; r.path_len = 1;
  memcpy(r.key, _channels[ch].key, LOG_KEY_LEN);
  snprintf(r.name, sizeof(r.name), "%s", sender);
  snprintf(r.text, sizeof(r.text), "%s", text);
  r.sender_ts = r.stored_at = _now;
  if (Sos::isSosText(text)) r.flags |= LOG_FLAG_SOS;
  append(r, true);
}

int SimModel::contactCount() { return (int)_contacts.size(); }
bool SimModel::contactAt(int i, ContactView& out) { if (i < 0 || i >= (int)_contacts.size()) return false; out = _contacts[i]; return true; }
bool SimModel::findContact(const uint8_t* key, ContactView& out) {
  for (auto& c : _contacts) if (!memcmp(c.key, key, LOG_KEY_LEN)) { out = c; return true; }
  return false;
}
void SimModel::setFavorite(const uint8_t* key, bool fav) { for (auto& c : _contacts) if (!memcmp(c.key, key, LOG_KEY_LEN)) c.favorite = fav; }
void SimModel::setShareLocation(const uint8_t* key, bool s) { for (auto& c : _contacts) if (!memcmp(c.key, key, LOG_KEY_LEN)) c.share_location = s; }
bool SimModel::removeContact(const uint8_t* key) {
  for (size_t i = 0; i < _contacts.size(); i++) if (!memcmp(_contacts[i].key, key, LOG_KEY_LEN)) { _contacts.erase(_contacts.begin() + i); return true; }
  return false;
}
int SimModel::channelCount() { return (int)_channels.size(); }
bool SimModel::channelAt(int i, ChannelView& out) { if (i < 0 || i >= (int)_channels.size()) return false; out = _channels[i]; return true; }
bool SimModel::findChannel(const uint8_t* key, ChannelView& out) {
  for (auto& c : _channels) if (!memcmp(c.key, key, LOG_KEY_LEN)) { out = c; return true; }
  return false;
}
bool SimModel::joinChannel(const char* name) {
  for (auto& c : _channels) if (!strcasecmp(c.name, name)) return true;   // already joined
  ChannelView ch = {};
  key6(ch.key, (uint8_t)(150 + _channels.size()));
  snprintf(ch.name, sizeof(ch.name), "%s", name);
  ch.idx = (int)_channels.size();
  _channels.push_back(ch);
  return true;
}
bool SimModel::leaveChannel(const uint8_t* key) {
  for (size_t i = 0; i < _channels.size(); i++) if (!memcmp(_channels[i].key, key, LOG_KEY_LEN)) { _channels.erase(_channels.begin() + i); return true; }
  return false;
}

void SimModel::nodeId(char* out, int n) { snprintf(out, n, "A1B2C3D4"); }
RadioSettings SimModel::radio() { return _radio; }
void SimModel::setRadio(const RadioPreset& p) { _radio.freq = p.freq; _radio.bw = p.bw; _radio.sf = p.sf; _radio.cr = p.cr; }

DeviceStatus SimModel::status() {
  DeviceStatus s = {};
  s.battery_pct = 82; s.battery_mv = 3990; s.bt_enabled = _bt; s.bt_connected = false;
  s.charging = _charging;
  s.gps_enabled = _gps; s.gps_fix = _gps; s.gps_sats = 9; s.lat = -31.9110; s.lon = 152.4630;
  s.sd_card = true; s.under_launcher = true; s.now = _now; s.clock_valid = true;
  s.free_heap_kb = 120; s.free_psram_kb = 7400; s.contacts = (int)_contacts.size(); s.messages_stored = (uint32_t)_log.size();
  return s;
}

int SimModel::recentlyHeard(HeardView* out, int max) {
  int n = 0;
  for (auto& c : _contacts) {
    if (n >= max) break;
    HeardView& h = out[n++];
    memset(&h, 0, sizeof(h));
    snprintf(h.name, sizeof(h.name), "%s", c.name);
    memcpy(h.key, c.key, LOG_KEY_LEN);
    h.when = c.last_heard;
    h.hops = c.hops == 0xFF ? 3 : c.hops;
  }
  std::sort(out, out + n, [](const HeardView& a, const HeardView& b) { return a.when > b.when; });
  return n;
}
