#include <esp_random.h>
#include "EasyMesh.h"
#include <helpers/AdvertDataHelpers.h>
#include <SHA256.h>
#include "store/Storage.h"

int EasyMesh::findChannelByKey(const uint8_t* key6, ChannelDetails* out) {
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!getChannel(i, ch)) continue;
    if (ch.name[0] == 0) continue;
    if (memcmp(ch.channel.secret, key6, LOG_KEY_LEN) == 0) {
      if (out) *out = ch;
      return i;
    }
  }
  return -1;
}

bool EasyMesh::removeContactByKey(const uint8_t* key6) {
  ContactInfo* c = contactByKey(key6);
  if (!c) return false;
  bool ok = removeContact(*c);
  return ok;
}

int32_t EasyMesh::sendFromDevice(const ConvKey& to, const char* text) {
  if (!_log || !text || !*text) return -1;

  LogRecord r;
  memset(&r, 0, sizeof(r));
  r.kind = to.kind;
  r.dir = DIR_OUT;
  r.origin = ORIGIN_DEVICE;
  r.path_len = 0xFF;
  memcpy(r.key, to.id, LOG_KEY_LEN);
  strncpy(r.text, text, LOG_TEXT_MAX);
  if (strncmp(text, "LOC ", 4) == 0) r.flags |= LOG_FLAG_SILENT;   // location beacon: not a chat message
  r.sender_ts = getRTCClock()->getCurrentTimeUnique();

  if (to.kind == KIND_CHANNEL) {
    ChannelDetails ch;
    int idx = findChannelByKey(to.id, &ch);
    if (idx < 0) return -1;
    r.channel_idx = idx;
    strncpy(r.name, getNodeName(), LOG_NAME_MAX - 1);
    bool ok = sendGroupMessage(r.sender_ts, ch.channel, getNodeName(), r.text, strlen(r.text));
    if (ok && millis() - _last_grp_ms < 1000) { r.ack = _last_grp_hash; r.path_len = 0; }   // echo count starts at 0
    r.status = ok ? ST_SENT : ST_FAILED;
    return _log->append(r);
  }

  ContactInfo* c = contactByKey(to.id);
  if (!c) return -1;
  strncpy(r.name, c->name, LOG_NAME_MAX - 1);
  r.status = ST_PENDING;
  uint32_t seq = _log->append(r);

  // find a free outbox slot (evict the oldest if full)
  int slot = -1;
  for (int i = 0; i < OUTBOX_SIZE; i++) if (!_outbox[i].used) { slot = i; break; }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < OUTBOX_SIZE; i++) if (_outbox[i].seq < _outbox[slot].seq) slot = i;
    finish(_outbox[slot], ST_FAILED);
  }
  OutboxEntry& e = _outbox[slot];
  memset(&e, 0, sizeof(e));
  e.used = true;
  e.seq = seq;
  memcpy(e.key, c->id.pub_key, PUB_KEY_SIZE);
  e.timestamp = r.sender_ts;
  e.attempt = 0;
  int t = transmit(e);
  if (t < 0) finish(e, ST_FAILED);
  else if (t == 0) { e.busy = 1; e.deadline = millis() + 700 + (seq % 8) * 150; }   // stagger bursts (SOS fan-out)
  return seq;
}

int EasyMesh::transmit(OutboxEntry& e) {
  ContactInfo* c = lookupContactByPubKey(e.key, PUB_KEY_SIZE);
  LogRecord r;
  if (!c || !_log->get(e.seq, r)) return -1;

  // After two unanswered direct attempts, forget the stored route and flood instead.
  if (e.attempt == OUTBOX_MAX_ATTEMPTS - 1 && c->out_path_len != OUT_PATH_UNKNOWN) resetPathTo(*c);

  uint32_t expected_ack = 0, est_timeout = 0;
  int res = sendMessage(*c, e.timestamp, e.attempt, r.text, expected_ack, est_timeout);
  if (res == MSG_SEND_FAILED) return 0;   // packet pool full: try again shortly
  if (e.attempt < 4) e.acks[e.attempt] = expected_ack;
  e.deadline = millis() + max(est_timeout, (uint32_t)4000) + 2000;
  e.attempt++;

  r.ack = expected_ack;
  r.attempts = e.attempt;
  r.status = ST_PENDING;
  _log->update(r);
  return 1;
}

void EasyMesh::finish(OutboxEntry& e, uint8_t status) {
  LogRecord r;
  if (_log && _log->get(e.seq, r)) {
    r.status = status;
    _log->update(r);
  }
  e.used = false;
}

ContactInfo* EasyMesh::processAck(const uint8_t* data) {
  uint32_t ack;
  memcpy(&ack, data, 4);
  for (int i = 0; i < OUTBOX_SIZE; i++) {
    OutboxEntry& e = _outbox[i];
    if (!e.used) continue;
    for (int a = 0; a < 4 && a < e.attempt; a++) {
      if (e.acks[a] == ack) {
        ContactInfo* c = lookupContactByPubKey(e.key, PUB_KEY_SIZE);
        finish(e, ST_DELIVERED);
        MyMesh::processAck(data);   // let listeners see it too (harmless if unmatched)
        return c;
      }
    }
  }
  ContactInfo* c = MyMesh::processAck(data);   // phone-sent messages (stock ack table)
  if (_log) {   // mark phone-sent message delivered in the shared history
    LogRecord r;
    if (_log->findByAck(ack, r) && r.status == ST_PENDING) {
      r.status = ST_DELIVERED;
      _log->update(r);
    }
  }
  return c;
}

void EasyMesh::loopOutbox() {
  unsigned long now = millis();
  for (int i = 0; i < OUTBOX_SIZE; i++) {
    OutboxEntry& e = _outbox[i];
    if (!e.used || (long)(now - e.deadline) < 0) continue;
    if (e.attempt >= OUTBOX_MAX_ATTEMPTS) { finish(e, ST_FAILED); continue; }
    int t = transmit(e);
    if (t < 0 || (t == 0 && ++e.busy > 20)) finish(e, ST_FAILED);
    else if (t == 0) e.deadline = now + 1000 + (e.seq % 8) * 150;
  }
}

void EasyMesh::advertFlood() {
  NodePrefs* p = getNodePrefs();
  mesh::Packet* pkt = p->advert_loc_policy == ADVERT_LOC_NONE ? createSelfAdvert(p->node_name)
                                                                : createSelfAdvert(p->node_name, sensors.node_lat, sensors.node_lon);
  if (!pkt) return;
  // Adverts go out UNSCOPED: a scoped flood is only forwarded by repeaters that define that
  // exact region name, and an advert nobody forwards is the classic "nobody can see me".
  // Messages still use the user's region scope.
  TransportKey none;
  memset(none.key, 0, sizeof(none.key));
  trackEcho(pkt, 1);   // before sending: the packet is released once it's on air
  sendFloodScoped(none, pkt, 0);
}

void EasyMesh::setDefaultScope(const char* name) {
  NodePrefs* p = getNodePrefs();
  memset(p->default_scope_name, 0, sizeof(p->default_scope_name));
  memset(p->default_scope_key, 0, sizeof(p->default_scope_key));
  if (name && *name) {
    strncpy(p->default_scope_name, name, sizeof(p->default_scope_name) - 1);
    // key = first 16 bytes of SHA256("#" + name), as MeshCore's TransportKeyStore does
    char tag[40];
    snprintf(tag, sizeof(tag), "#%s", name);
    SHA256 sha;
    sha.update(tag, strlen(tag));
    sha.finalize(p->default_scope_key, sizeof(p->default_scope_key));
  }
  savePrefs();
}

int EasyMesh::joinSecretChannel(const char* name, const uint8_t* secret16) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {   // already joined?
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] && memcmp(ch.channel.secret, secret16, 16) == 0) return i;
  }
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] == 0) {
      memset(&ch, 0, sizeof(ch));
      memcpy(ch.channel.secret, secret16, 16);
      strncpy(ch.name, name, sizeof(ch.name) - 1);
      if (setChannel(i, ch)) return i;
    }
  }
  return -1;
}

int EasyMesh::joinHashtagChannel(const char* name) {
  // hashtag channel secret = first 16 bytes of SHA256(name incl. '#')
  uint8_t secret[32];
  SHA256 sha;
  sha.update(name, strlen(name));
  sha.finalize(secret, 32);
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {   // already joined?
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] && memcmp(ch.channel.secret, secret, 16) == 0) return i;
  }
  // first free slot (an empty name marks an unused slot)
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (getChannel(i, ch) && ch.name[0] == 0) {
      memset(&ch, 0, sizeof(ch));
      memcpy(ch.channel.secret, secret, 16);   // 128-bit key; setChannel computes the hash
      strncpy(ch.name, name, sizeof(ch.name) - 1);
      if (setChannel(i, ch)) return i;
    }
  }
  return -1;
}

bool EasyMesh::leaveChannelByKey(const uint8_t* key6) {
  int idx = findChannelByKey(key6, nullptr);
  if (idx < 0) return false;
  ChannelDetails empty;
  memset(&empty, 0, sizeof(empty));
  return setChannel(idx, empty);
}

bool EasyMesh::requestTelemetry(const uint8_t* key6) {
  ContactInfo* c = contactByKey(key6);
  if (!c) return false;
  uint32_t tag, est;
  return sendRequest(*c, (uint8_t)REQ_TYPE_GET_TELEMETRY_DATA, tag, est) != MSG_SEND_FAILED;
}

void EasyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  MyMesh::logRxRaw(snr, rssi, raw, len);
  if (len < 2) return;
  RxEntry& e = _rx[_rx_head];
  e.at_ms = millis();
  e.snr = snr;
  e.rssi = rssi;
  e.len = (uint8_t)len;
  e.header = raw[0];
  uint8_t route = raw[0] & PH_ROUTE_MASK;
  int pl = (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) ? 5 : 1;
  e.path_len = pl < len ? raw[pl] : 0;
  _rx_head = (_rx_head + 1) % RX_LOG_SIZE;
  if (_rx_count < RX_LOG_SIZE) _rx_count++;
}

int EasyMesh::rxLog(RxEntry* out, int max) const {
  int n = 0;
  for (int k = 0; k < _rx_count && n < max; k++) {
    int i = (_rx_head - 1 - k + RX_LOG_SIZE) % RX_LOG_SIZE;
    out[n++] = _rx[i];
  }
  return n;
}

#define CTL_DISCOVER_REQ 0x80
#define CTL_DISCOVER_RESP 0x90

bool EasyMesh::discover() {
  uint8_t req[6];
  req[0] = CTL_DISCOVER_REQ | 1;   // prefix-only responses
  req[1] = 0xFF;                   // any node type
  getRNG()->random((uint8_t*)&_disc_tag, 4);
  memcpy(&req[2], &_disc_tag, 4);
  _disc_n = 0;
  mesh::Packet* p = createControlData(req, sizeof(req));
  if (!p) return false;
  sendZeroHop(p);
  _disc_ms = millis();
  return true;
}

void EasyMesh::onControlDataRecv(mesh::Packet* packet) {
  MyMesh::onControlDataRecv(packet);
  if (packet->payload_len >= 14 && (packet->payload[0] & 0xF0) == CTL_DISCOVER_RESP &&
      memcmp(&packet->payload[2], &_disc_tag, 4) == 0) {
    for (int i = 0; i < _disc_n; i++) if (memcmp(_disc[i].prefix, &packet->payload[6], 8) == 0) return;   // dup
    if (_disc_n >= DISC_MAX) return;
    Discovered& d = _disc[_disc_n++];
    memcpy(d.prefix, &packet->payload[6], 8);
    d.type = packet->payload[0] & 0x0F;
    d.snr_them = ((int8_t)packet->payload[1]) / 4.0f;   // how well they heard us
    d.snr_us = packet->getSNR();                         // how well we hear them
    d.at_ms = millis();
  }
}

int EasyMesh::discovered(Discovered* out, int max) const {
  int n = _disc_n < max ? _disc_n : max;
  for (int i = 0; i < n; i++) out[i] = _disc[i];
  return n;
}

void EasyMesh::onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp,
                                   const uint8_t* sender_prefix, const char* text) {
  memcpy(_author, sender_prefix, 4);
  _author_valid = true;
  MyMesh::onSignedMessageRecv(from, pkt, sender_timestamp, sender_prefix, text);
  _author_valid = false;
}

// ---------------------------------------------------------------- per-channel region scope
#define CHSCOPE_FILE "/easydeck_chscope.bin"

static void scopeKeyFor(const char* name, uint8_t out[16]) {   // same as MeshCore's TransportKeyStore
  char tag[40];
  snprintf(tag, sizeof(tag), "#%s", name);
  SHA256 sha;
  sha.update(tag, strlen(tag));
  sha.finalize(out, 16);
}

void EasyMesh::saveChannelScopes() {
  File f = DataFS.open(CHSCOPE_FILE, "w");
  if (!f) return;
  f.write((const uint8_t*)&_n_cscope, sizeof(_n_cscope));
  f.write((const uint8_t*)_cscope, sizeof(ChanScope) * _n_cscope);
  f.close();
}

void EasyMesh::loadChannelScopes() {
  File f = DataFS.open(CHSCOPE_FILE, "r");
  if (!f) return;
  int n = 0;
  if (f.read((uint8_t*)&n, sizeof(n)) == sizeof(n) && n >= 0 && n <= MAX_CHAN_SCOPES &&
      f.read((uint8_t*)_cscope, sizeof(ChanScope) * n) == (int)(sizeof(ChanScope) * n)) _n_cscope = n;
  f.close();
}

void EasyMesh::setChannelScope(const uint8_t* key6, int mode, const char* name) {
  int i = 0;
  for (; i < _n_cscope; i++) if (memcmp(_cscope[i].key, key6, LOG_KEY_LEN) == 0) break;
  if (mode == 0) {   // back to default: drop the entry
    if (i < _n_cscope) { _cscope[i] = _cscope[--_n_cscope]; saveChannelScopes(); }
    return;
  }
  if (i == _n_cscope) {
    if (_n_cscope == MAX_CHAN_SCOPES) return;
    _n_cscope++;
  }
  ChanScope& c = _cscope[i];
  memcpy(c.key, key6, LOG_KEY_LEN);
  c.mode = (uint8_t)mode;
  memset(c.name, 0, sizeof(c.name));
  memset(c.tkey, 0, sizeof(c.tkey));
  if (mode == 2 && name && *name) { strncpy(c.name, name, sizeof(c.name) - 1); scopeKeyFor(c.name, c.tkey); }
  else if (mode == 2) c.mode = 1;
  saveChannelScopes();
}

int EasyMesh::channelScope(const uint8_t* key6, char* out) const {
  out[0] = 0;
  for (int i = 0; i < _n_cscope; i++) {
    if (memcmp(_cscope[i].key, key6, LOG_KEY_LEN) != 0) continue;
    strncpy(out, _cscope[i].name, 31);
    out[31] = 0;
    return _cscope[i].mode;
  }
  return 0;
}

void EasyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  _last_grp_hash = trackEcho(pkt, 0);
  _last_grp_ms = millis();
  // a group message sent from the phone was logged by SyncInterface just before: link it
  if (_log && _log->nextSeq() > 0) {
    LogRecord r;
    if (_log->get(_log->nextSeq() - 1, r) && r.dir == DIR_OUT && r.kind == KIND_CHANNEL && r.ack == 0 &&
        r.origin == ORIGIN_PHONE && getRTCClock()->getCurrentTime() - r.stored_at < 5) {
      r.ack = _last_grp_hash;
      r.path_len = 0;
      _log->update(r);
    }
  }
  for (int i = 0; i < _n_cscope; i++) {
    if (memcmp(_cscope[i].key, channel.secret, LOG_KEY_LEN) != 0) continue;
    TransportKey k;
    if (_cscope[i].mode == 1) memset(k.key, 0, sizeof(k.key));   // unscoped
    else memcpy(k.key, _cscope[i].tkey, sizeof(k.key));
    MyMesh::sendFloodScoped(k, pkt, delay_millis);
    return;
  }
  MyMesh::sendFloodScoped(channel, pkt, delay_millis);   // default behaviour
}

// ---------------------------------------------------------------- echoes ("heard by N repeaters")
uint32_t EasyMesh::trackEcho(mesh::Packet* pkt, uint8_t kind) {
  uint8_t hash[MAX_HASH_SIZE];
  pkt->calculatePacketHash(hash);
  uint32_t h;
  memcpy(&h, hash, 4);
  if (!h) h = 1;   // 0 means "no echo tracking" in records
  Echo& e = _echo[_echo_head];
  _echo_head = (_echo_head + 1) % ECHO_SLOTS;
  e.h = h; e.ms = millis(); e.kind = kind; e.count = 0;
  return h;
}

void EasyMesh::logRx(mesh::Packet* pkt, int len, float score) {
  MyMesh::logRx(pkt, len, score);
  if (!pkt->isRouteFlood()) return;   // repeaters re-send floods
  uint32_t now = millis();
  bool any = false;
  for (auto& e : _echo) if (e.h && now - e.ms < ECHO_WINDOW_MS) { any = true; break; }
  if (!any) return;
  uint8_t hash[MAX_HASH_SIZE];
  pkt->calculatePacketHash(hash);
  uint32_t h;
  memcpy(&h, hash, 4);
  if (!h) h = 1;
  for (auto& e : _echo) {
    if (e.h != h || now - e.ms >= ECHO_WINDOW_MS) continue;
    if (e.count < 255) e.count++;
    if (e.kind == 0 && _log) {   // group message: update the record so the chat shows it
      LogRecord r;
      if (_log->findByAck(h, r) && r.kind == KIND_CHANNEL) {
        r.path_len = e.count;
        _log->update(r);
      }
    }
    break;
  }
}

EasyMesh::EchoInfo EasyMesh::advertEcho() const {
  EchoInfo out = { false, 0, 0 };
  uint32_t best = 0;
  for (auto& e : _echo) {
    if (!e.h || e.kind != 1) continue;
    if (!out.active || (int32_t)(e.ms - best) > 0) { out.active = true; best = e.ms; out.count = e.count; }
  }
  if (out.active) out.age_s = (millis() - best) / 1000;
  return out;
}

bool EasyMesh::idleForSleep() const {
  for (int i = 0; i < OUTBOX_SIZE; i++) if (_outbox[i].used) return false;
  return _mgr->getOutboundTotal() == 0;
}

// ---------------------------------------------------------------- route ping (TRACE)
// A TRACE packet follows the listed repeater hashes; each repeater appends the SNR it heard
// it with; when the list is used up, whoever hears the last hop gets onTraceRecv. Listing the
// route out and back (A,B,A) makes the last hop transmit back to us: a round trip with the
// SNR of every link in both directions.
bool EasyMesh::startTrace(const uint8_t* key6) {
  ContactInfo* c = contactByKey(key6);
  if (!c || c->out_path_len == OUT_PATH_UNKNOWN) { _trace = TraceResult(); memcpy(_trace.key, key6, LOG_KEY_LEN); _trace.state = 4; return false; }
  int hops = c->out_path_len & 0x3F, sz = (c->out_path_len >> 6) + 1;
  uint8_t path_sz = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 0xFF;   // TRACE supports 1/2/4-byte hashes
  if (hops == 0 || path_sz == 0xFF || hops * 2 - 1 > TRACE_MAX_HOPS) { _trace = TraceResult(); memcpy(_trace.key, key6, LOG_KEY_LEN); _trace.state = 4; return false; }
  uint8_t path[TRACE_MAX_HOPS * 4];
  int n = 0;
  for (int h = 0; h < hops; h++) { memcpy(&path[n * sz], &c->out_path[h * sz], sz); n++; }          // out
  for (int h = hops - 2; h >= 0; h--) { memcpy(&path[n * sz], &c->out_path[h * sz], sz); n++; }     // and back
  _trace_tag = (uint32_t)esp_random() | 1;
  mesh::Packet* pkt = createTrace(_trace_tag, 0, path_sz);
  _trace = TraceResult();
  memcpy(_trace.key, key6, LOG_KEY_LEN);
  if (!pkt) { _trace.state = 4; return false; }
  sendDirect(pkt, path, n * sz);
  _trace.state = 1;
  _trace_ms = millis();
  _trace_timeout = 4000 + 2500UL * n;   // generous: every hop waits for its turn on air
  return true;
}

void EasyMesh::onTraceRecv(mesh::Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                           const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  MyMesh::onTraceRecv(packet, tag, auth_code, flags, path_snrs, path_hashes, path_len);   // phone app too
  if (_trace.state != 1 || tag != _trace_tag) return;
  uint8_t path_sz = flags & 0x03;
  int links = path_len >> path_sz;   // hops recorded by the repeaters
  if (links > TRACE_MAX_HOPS) links = TRACE_MAX_HOPS;
  for (int i = 0; i < links; i++) _trace.snr4[i] = (int8_t)path_snrs[i];
  _trace.snr4[links] = (int8_t)(packet->getSNR() * 4);   // last repeater -> us
  _trace.hops = links + 1;
  _trace.rtt_ms = millis() - _trace_ms;
  _trace.state = 2;
}

TraceResult EasyMesh::traceResult() {
  if (_trace.state == 1 && millis() - _trace_ms > _trace_timeout) _trace.state = 3;
  return _trace;
}

// MeshCore reports is_new=false for a node it has just auto-added, so remember whether the
// advert's node was already a contact before the base class handles it.
void EasyMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp,
                            const uint8_t* app_data, size_t app_data_len) {
  _advert_was_known = lookupContactByPubKey(id.pub_key, PUB_KEY_SIZE) != nullptr;
  {   // time sample from repeaters / room servers (companions' clocks are often wrong)
    AdvertDataParser parser(app_data, app_data_len);
    uint8_t t = parser.isValid() ? parser.getType() : 0;
    if ((t == ADV_TYPE_REPEATER || t == ADV_TYPE_ROOM) && timestamp > 1767225600UL) {   // after 2026-01-01
      TimeSample& s = _tsamp[_tsamp_head];
      _tsamp_head = (_tsamp_head + 1) % 8;
      s.who[0] = id.pub_key[0]; s.who[1] = id.pub_key[1];
      s.offset = (int32_t)(timestamp - getRTCClock()->getCurrentTime());
      s.at_ms = millis() | 1;
    }
  }
  MyMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
  _advert_was_known = true;
}

bool EasyMesh::meshTime(uint32_t& now_utc, int& agreeing) const {
  int32_t off[8];
  int n = 0;
  uint32_t ms = millis();
  for (int i = 0; i < 8; i++) {
    const TimeSample& s = _tsamp[i];
    if (!s.at_ms || ms - s.at_ms > 30UL * 60000UL) continue;
    bool dup = false;   // one vote per repeater (latest sample wins only once)
    for (int j = 0; j < i && !dup; j++)
      dup = _tsamp[j].at_ms && ms - _tsamp[j].at_ms <= 30UL * 60000UL && _tsamp[j].who[0] == s.who[0] && _tsamp[j].who[1] == s.who[1];
    if (!dup) off[n++] = s.offset;
  }
  if (n < 3) return false;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (off[b] < off[a]) { int32_t t = off[a]; off[a] = off[b]; off[b] = t; }
  int32_t med = off[n / 2];
  agreeing = 0;
  for (int i = 0; i < n; i++) if (off[i] - med <= 120 && med - off[i] <= 120) agreeing++;
  if (agreeing < 3) return false;
  now_utc = getRTCClock()->getCurrentTime() + med;
  return true;
}
