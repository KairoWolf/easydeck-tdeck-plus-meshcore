#include "SyncInterface.h"
#include "EasyDeckProtocol.h"
#include "EasyMesh.h"
#include "store/Storage.h"

// stock MeshCore companion codes we observe (from examples/companion_radio/MyMesh.cpp)
#define MC_CMD_SEND_TXT_MSG          2
#define MC_CMD_SEND_CHANNEL_TXT_MSG  3
#define MC_RESP_CODE_OK              0
#define MC_RESP_CODE_ERR             1
#define MC_RESP_CODE_SENT            6
#define MC_TXT_TYPE_PLAIN            0
#define MC_CMD_FACTORY_RESET         51

#define PHONE_ACK_TIMEOUT_MS   (3UL * 60UL * 1000UL)   // after the phone's last attempt

static inline void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static inline uint32_t get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

bool SyncInterface::sendNow(const uint8_t* buf, size_t len) {
  if (!_inner.isConnected() || _inner.isWriteBusy()) return false;
  return _inner.writeFrame(buf, len) > 0;
}

size_t SyncInterface::writeFrame(const uint8_t src[], size_t len) {
  // Watch MyMesh's replies to phone-initiated sends, to track their status.
  if (_awaiting_seq >= 0 && len > 0) {
    LogRecord r;
    bool have = _log.get((uint32_t)_awaiting_seq, r);
    if (src[0] == MC_RESP_CODE_SENT && len >= 10 && !_awaiting_is_channel) {
      if (have) {
        r.ack = get32(&src[2]);
        r.status = ST_PENDING;
        _log.update(r);
        // remember for timeout sweep
        int i = 0;
        for (; i < _phone_pending_n; i++) if (_phone_pending[i].seq == r.seq) break;
        if (i == _phone_pending_n && _phone_pending_n < PHONE_PENDING_MAX) _phone_pending_n++;
        if (i < PHONE_PENDING_MAX) _phone_pending[i] = { r.seq, millis() };
      }
      _awaiting_seq = -1;
    } else if (src[0] == MC_RESP_CODE_OK && _awaiting_is_channel) {
      if (have) { r.status = ST_SENT; _log.update(r); }
      _awaiting_seq = -1;
    } else if (src[0] == MC_RESP_CODE_ERR) {
      if (have) { r.status = ST_FAILED; _log.update(r); }
      _awaiting_seq = -1;
    }
  }
  return _inner.writeFrame(src, len);
}

size_t SyncInterface::checkRecvFrame(uint8_t dest[]) {
  size_t len = _inner.checkRecvFrame(dest);
  if (len == 0) return 0;

  if (dest[0] >= EDS_CMD_HELLO && dest[0] <= EDS_CMD_GET_RECORD) {
    handleEds(dest, len);
    return 0;   // consumed — MyMesh never sees EDS commands
  }
  if (dest[0] == MC_CMD_FACTORY_RESET && len >= 6 && memcmp(&dest[1], "reset", 5) == 0) {
    // MeshCore's own handler casts the store to SPIFFSFS and formats it, which is
    // unsafe with our SD-backed storage. Do a safe EasyDeck-only reset instead.
    _inner.disable();
    storageFactoryReset();
    delay(500);
    ESP.restart();
  }
  if (dest[0] == MC_CMD_SEND_TXT_MSG) notePhoneDirectSend(dest, len);
  else if (dest[0] == MC_CMD_SEND_CHANNEL_TXT_MSG) notePhoneChannelSend(dest, len);
  return len;
}

void SyncInterface::notePhoneDirectSend(const uint8_t* f, size_t len) {
  // [cmd, txt_type, attempt, ts u32, pubkey_prefix[6], text...]
  if (len < 14 || f[1] != MC_TXT_TYPE_PLAIN) return;
  uint8_t attempt = f[2];
  uint32_t ts = get32(&f[3]);
  ConvKey k = MessageLog::directKey(&f[7]);

  LogRecord r;
  if (_log.findOutgoing(k, ts, r)) {            // a retry of a message we already logged
    r.attempts = attempt + 1;
    r.status = ST_PENDING;
    _log.update(r);
    _awaiting_seq = r.seq;
    _awaiting_is_channel = false;
    return;
  }
  memset(&r, 0, sizeof(r));
  r.kind = KIND_DIRECT;
  r.dir = DIR_OUT;
  r.status = ST_PENDING;
  r.origin = ORIGIN_PHONE;
  r.sender_ts = ts;
  r.attempts = attempt + 1;
  r.path_len = 0xFF;
  memcpy(r.key, &f[7], LOG_KEY_LEN);
  ContactInfo* c = easy_mesh.lookupContactByPubKey(&f[7], 6);
  if (c) strncpy(r.name, c->name, LOG_NAME_MAX - 1);
  size_t tlen = min((size_t)LOG_TEXT_MAX, len - 13);
  memcpy(r.text, &f[13], tlen);
  r.text[tlen] = 0;
  _awaiting_seq = _log.append(r);
  _awaiting_is_channel = false;
}

void SyncInterface::notePhoneChannelSend(const uint8_t* f, size_t len) {
  // [cmd, txt_type, channel_idx, ts u32, text...]
  if (len < 8 || f[1] != MC_TXT_TYPE_PLAIN) return;
  ChannelDetails ch;
  if (!easy_mesh.getChannel(f[2], ch)) return;
  LogRecord r;
  memset(&r, 0, sizeof(r));
  r.kind = KIND_CHANNEL;
  r.dir = DIR_OUT;
  r.status = ST_PENDING;
  r.origin = ORIGIN_PHONE;
  r.sender_ts = get32(&f[3]);
  r.channel_idx = f[2];
  r.path_len = 0xFF;
  memcpy(r.key, ch.channel.secret, LOG_KEY_LEN);
  strncpy(r.name, easy_mesh.getNodeName(), LOG_NAME_MAX - 1);
  size_t tlen = min((size_t)LOG_TEXT_MAX, len - 7);
  memcpy(r.text, &f[7], tlen);
  r.text[tlen] = 0;
  _awaiting_seq = _log.append(r);
  _awaiting_is_channel = true;
}

size_t SyncInterface::encodeRecord(uint8_t code, const LogRecord& r, uint8_t* out) {
  size_t i = 0;
  out[i++] = code;
  put32(&out[i], r.seq); i += 4;
  put32(&out[i], r.stored_at); i += 4;
  put32(&out[i], r.sender_ts); i += 4;
  out[i++] = r.kind;
  out[i++] = r.dir;
  out[i++] = r.status;
  out[i++] = r.origin;
  memcpy(&out[i], r.key, LOG_KEY_LEN); i += LOG_KEY_LEN;
  out[i++] = r.channel_idx;
  out[i++] = r.flags;
  out[i++] = (uint8_t)r.snr4;
  out[i++] = r.path_len;
  size_t nlen = strnlen(r.name, LOG_NAME_MAX);
  out[i++] = (uint8_t)nlen;
  memcpy(&out[i], r.name, nlen); i += nlen;
  size_t tlen = strnlen(r.text, LOG_TEXT_MAX);
  if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
  memcpy(&out[i], r.text, tlen); i += tlen;
  return i;
}

void SyncInterface::handleEds(const uint8_t* f, size_t len) {
  uint8_t out[MAX_FRAME_SIZE];
  switch (f[0]) {
    case EDS_CMD_HELLO: {
      _eds_active = true;
      out[0] = EDS_RESP_HELLO;
      out[1] = EDS_VERSION;
      put32(&out[2], _log.logId());
      put32(&out[6], _log.firstSeq());
      put32(&out[10], _log.nextSeq());
      out[14] = _log.usingSD() ? 1 : 0;
      _inner.writeFrame(out, 15);
      break;
    }
    case EDS_CMD_GET_SINCE: {
      if (len < 5) break;
      _eds_active = true;
      uint32_t from = get32(&f[1]);
      uint8_t max = (len >= 6 && f[5]) ? f[5] : 32;
      if (from < _log.firstSeq()) from = _log.firstSeq();
      _stream_seq = from;
      _stream_left = max;
      _streaming = true;
      break;
    }
    case EDS_CMD_GET_RECORD: {
      if (len < 5) break;
      LogRecord r;
      if (_log.get(get32(&f[1]), r)) {
        _inner.writeFrame(out, encodeRecord(EDS_RESP_RECORD, r, out));
      } else {
        out[0] = EDS_RESP_END;
        put32(&out[1], _log.nextSeq());
        _inner.writeFrame(out, 5);
      }
      break;
    }
    case EDS_CMD_MARK_READ: {
      if (len < 2 + LOG_KEY_LEN) break;
      ConvKey k;
      k.kind = f[1];
      memcpy(k.id, &f[2], LOG_KEY_LEN);
      int n = _log.markConversationRead(k);   // UI observer refreshes badges
      out[0] = EDS_RESP_OK;
      out[1] = n & 0xFF;
      out[2] = (n >> 8) & 0xFF;
      _inner.writeFrame(out, 3);
      break;
    }
  }
}

void SyncInterface::onLogAppended(const LogRecord& r) {
  if (_eds_active) _push_new = true;
}

void SyncInterface::onLogUpdated(const LogRecord& r) {
  if (!_eds_active) return;
  for (int i = 0; i < _update_q_len; i++) if (_update_q[i] == r.seq) return;
  if (_update_q_len < EDS_UPDATE_Q) _update_q[_update_q_len++] = r.seq;
  // if the queue overflows the app will still refresh statuses on its next sync
}

void SyncInterface::sweepPhonePending() {
  unsigned long now = millis();
  for (int i = 0; i < _phone_pending_n; ) {
    LogRecord r;
    bool have = _log.get(_phone_pending[i].seq, r);
    bool done = !have || r.status != ST_PENDING;
    if (!done && now - _phone_pending[i].last_attempt > PHONE_ACK_TIMEOUT_MS) {
      r.status = ST_FAILED;
      _log.update(r);
      done = true;
    }
    if (done) _phone_pending[i] = _phone_pending[--_phone_pending_n];
    else i++;
  }
}

void SyncInterface::loop() {
  bool conn = _inner.isConnected();
  bool edge = false;   // the aggregate stays "connected" while USB is plugged in, so watch each link
  for (int i = 0; i < 2; i++) {
    if (!_links[i]) continue;
    bool c = _links[i]->isConnected();
    if (c != _link_was[i]) { _link_was[i] = c; edge = true; }
  }
  if (conn != _was_connected || edge) {
    _was_connected = conn;
    _eds_active = false;    // every new connection must say HELLO again
    _streaming = false;
    _push_new = false;
    _update_q_len = 0;
  }

  if ((long)(millis() - _next_sweep) >= 0) {
    _next_sweep = millis() + 10000;
    sweepPhonePending();
  }

  if (!conn || _inner.isWriteBusy()) return;
  uint8_t out[MAX_FRAME_SIZE];

  if (_streaming) {   // one frame per loop iteration keeps the radio responsive
    LogRecord r;
    while (_stream_left > 0 && _stream_seq < _log.nextSeq() && !_log.get(_stream_seq, r)) _stream_seq++;  // skip holes
    if (_stream_left > 0 && _stream_seq < _log.nextSeq()) {
      if (sendNow(out, encodeRecord(EDS_RESP_RECORD, r, out))) { _stream_seq++; _stream_left--; }
    } else {
      out[0] = EDS_RESP_END;
      put32(&out[1], _stream_seq);
      if (sendNow(out, 5)) _streaming = false;
    }
    return;
  }
  if (!_eds_active) return;
  if (_update_q_len > 0) {
    LogRecord r;
    if (_log.get(_update_q[0], r)) {
      if (!sendNow(out, encodeRecord(EDS_PUSH_UPDATE, r, out))) return;
    }
    memmove(&_update_q[0], &_update_q[1], (--_update_q_len) * sizeof(uint32_t));
    return;
  }
  if (_push_new) {
    out[0] = EDS_PUSH_NEW;
    put32(&out[1], _log.nextSeq());
    if (sendNow(out, 5)) _push_new = false;
  }
}
