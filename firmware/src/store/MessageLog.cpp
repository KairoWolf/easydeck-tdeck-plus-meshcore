#include "MessageLog.h"
#include <SD.h>
#include <SPI.h>
#include <esp_random.h>
#include <target.h>
#include "Storage.h"
#include "ui/TextInput.h"

#define LOG_MAGIC        0x4D445945UL   // "EYDM"
#define META_MAGIC       0x4D445945UL
#define SD_CAPACITY      20000
#define FLASH_CAPACITY   300
#define CACHE_RECORDS    1500           // ~384 KB of PSRAM
#define SD_FREQ_HZ       20000000

struct __attribute__((packed)) LogMeta {   // stored with safeSave(): two copies + checksum
  uint32_t log_id;
  uint32_t next_seq;
  uint32_t capacity;
};

bool MessageLog::begin(fs::FS& internal_fs) {
  bool ok = false;
  // SD card (mounted earlier by storage setup) shares the SPI bus with display and radio.
  if (sdPresent()) {
    SD.mkdir("/easydeck");
    ok = openStore(SD, "/easydeck", SD_CAPACITY);
    _on_sd = ok;
  }
  if (!ok) {
    ok = openStore(internal_fs, "", FLASH_CAPACITY);
    _on_sd = false;
  }
  loadCache();
  return ok;
}

bool MessageLog::openStore(fs::FS& fs, const char* dir, uint32_t capacity) {
  _fs = &fs;
  _data_path = String(dir) + "/messages.log";
  _meta_path = String(dir) + "/messages.meta";
  _capacity = capacity;

  LogMeta meta = {};
  bool have_meta = safeLoad(fs, _meta_path.c_str(), META_MAGIC, &meta, sizeof(meta));
  if (have_meta && meta.capacity == capacity) {
    _log_id = meta.log_id;
    _next_seq = meta.next_seq;
  } else if (have_meta) {   // different layout: start over
    _log_id = esp_random();
    _next_seq = 0;
    fs.remove(_data_path);
  } else {
    // Meta missing or damaged (e.g. power loss mid-write): never throw the messages away.
    // Rebuild next_seq from the highest valid record (boot only, before the watchdog starts).
    _log_id = esp_random();   // phone app re-syncs everything once
    _next_seq = 0;
    File f = fs.open(_data_path, "r");
    if (f) {
      LogRecord r;
      uint32_t slots = f.size() / sizeof(LogRecord);
      if (slots > capacity) slots = capacity;
      bool any = false;
      uint32_t best = 0;
      for (uint32_t i = 0; i < slots; i++) {
        if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
        if (r.magic == LOG_MAGIC && r.seq % capacity == i && (!any || r.seq > best)) { best = r.seq; any = true; }
      }
      f.close();
      if (any) _next_seq = best + 1;
    }
  }

  if (!fs.exists(_data_path)) {
    File f = fs.open(_data_path, "w");
    if (!f) return false;
    f.close();
  }

  // Recover if the meta file lagged behind the data file (e.g. power loss).
  LogRecord r;
  while (readSlot(_next_seq, r) && r.seq == _next_seq) _next_seq++;

  saveMeta();
  return true;
}

void MessageLog::saveMeta() {
  LogMeta meta = { _log_id, _next_seq, _capacity };
  safeSave(*_fs, _meta_path.c_str(), META_MAGIC, &meta, sizeof(meta));
}

bool MessageLog::readSlot(uint32_t seq, LogRecord& out) {
  if (!_fs) return false;
  File f = _fs->open(_data_path, "r");
  if (!f) return false;
  uint32_t off = (seq % _capacity) * sizeof(LogRecord);
  bool ok = f.size() >= off + sizeof(LogRecord) && f.seek(off) &&
            f.read((uint8_t*)&out, sizeof(LogRecord)) == sizeof(LogRecord);
  f.close();
  return ok && out.magic == LOG_MAGIC && out.seq == seq;
}

bool MessageLog::writeSlot(const LogRecord& r) {
  File f = _fs->open(_data_path, "r+");
  if (!f) return false;
  uint32_t off = (r.seq % _capacity) * sizeof(LogRecord);
  // Pad if there is a gap (only happens after meta loss).
  if (f.size() < off) {
    f.seek(f.size());
    static const uint8_t zero[64] = {0};
    for (uint32_t n = f.size(); n < off; ) {
      uint32_t c = min((uint32_t)sizeof(zero), off - n);
      f.write(zero, c);
      n += c;
    }
  }
  bool ok = f.seek(off) && f.write((const uint8_t*)&r, sizeof(LogRecord)) == sizeof(LogRecord);
  f.close();
  return ok;
}

void MessageLog::loadCache() {
  _cache_n = min((uint32_t)CACHE_RECORDS, _capacity);   // a bigger cache would hold records already overwritten
  _cache = (LogRecord*)ps_calloc(_cache_n, sizeof(LogRecord));
  if (!_cache) {
    _cache_n = min((uint32_t)100, _capacity);
    _cache = (LogRecord*)calloc(_cache_n, sizeof(LogRecord));
  }
  if (!_cache) { _cache_n = 0; return; }

  uint32_t from = firstSeq();
  if (_next_seq > _cache_n && _next_seq - _cache_n > from) from = _next_seq - _cache_n;

  File f = _fs->open(_data_path, "r");
  if (!f) return;
  for (uint32_t s = from; s < _next_seq; s++) {
    uint32_t off = (s % _capacity) * sizeof(LogRecord);
    LogRecord& slot = _cache[s % _cache_n];
    if (f.position() != off) f.seek(off);
    if (f.read((uint8_t*)&slot, sizeof(LogRecord)) != sizeof(LogRecord) || slot.magic != LOG_MAGIC || slot.seq != s) {
      memset(&slot, 0, sizeof(LogRecord));
    }
  }
  f.close();
}

LogRecord* MessageLog::cached(uint32_t seq) {
  if (!_cache_n || seq >= _next_seq || seq < firstSeq()) return nullptr;
  LogRecord* r = &_cache[seq % _cache_n];
  return (r->magic == LOG_MAGIC && r->seq == seq) ? r : nullptr;
}

uint32_t MessageLog::append(LogRecord& r) {
  r.magic = LOG_MAGIC;
  r.seq = _next_seq;
  r.stored_at = rtc_clock.getCurrentTime();
  r.text[LOG_TEXT_MAX] = 0;
  r.text_len = strlen(r.text);
  r.name[LOG_NAME_MAX - 1] = 0;
  writeSlot(r);
  _next_seq++;
  saveMeta();
  if (_cache_n) _cache[r.seq % _cache_n] = r;
  notifyAppended(r);
  return r.seq;
}

bool MessageLog::get(uint32_t seq, LogRecord& out) {
  LogRecord* c = cached(seq);
  if (c) { out = *c; return true; }
  if (seq < firstSeq() || seq >= _next_seq) return false;
  return readSlot(seq, out);
}

bool MessageLog::update(const LogRecord& r) {
  if (r.seq < firstSeq() || r.seq >= _next_seq) return false;
  LogRecord* c = cached(r.seq);
  if (c) *c = r;
  bool ok = writeSlot(r);
  notifyUpdated(r);
  return ok;
}

bool MessageLog::findByAck(uint32_t ack, LogRecord& out) {
  if (!ack || !_cache_n) return false;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  for (uint32_t s = _next_seq; s-- > lo; ) {
    LogRecord* r = cached(s);
    if (r && r->dir == DIR_OUT && r->ack == ack) { out = *r; return true; }
  }
  return false;
}

bool MessageLog::findOutgoing(const ConvKey& k, uint32_t sender_ts, LogRecord& out) {
  if (!_cache_n) return false;
  uint32_t lo = _next_seq > 200 ? _next_seq - 200 : 0;   // retries are always recent
  for (uint32_t s = _next_seq; s-- > lo; ) {
    LogRecord* r = cached(s);
    if (r && r->dir == DIR_OUT && r->sender_ts == sender_ts && r->conv() == k) { out = *r; return true; }
  }
  return false;
}

int MessageLog::markConversationRead(const ConvKey& k) {
  int changed = 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  File f;   // opened lazily, once, for all the slot rewrites
  LogRecord last;
  _bulk = true;   // UI observers skip per-record refreshes; one final notification below
  for (uint32_t s = lo; s < _next_seq; s++) {
    LogRecord* r = cached(s);
    if (r && r->dir == DIR_IN && !r->isRead() && r->conv() == k) {
      r->flags |= LOG_FLAG_READ;
      if (!f) f = _fs->open(_data_path, "r+");
      if (f) {
        uint32_t off = (r->seq % _capacity) * sizeof(LogRecord);
        if (f.seek(off)) f.write((const uint8_t*)r, sizeof(LogRecord));
      }
      notifyUpdated(*r);
      last = *r;
      if (++changed % 32 == 0) feedLoopWDT();   // hundreds of SD/flash rewrites can take seconds
    }
  }
  if (f) f.close();
  _bulk = false;
  if (changed) notifyUpdated(last);
  return changed;
}

int MessageLog::unreadCount(const ConvKey& k) {
  int n = 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  for (uint32_t s = lo; s < _next_seq; s++) {
    LogRecord* r = cached(s);
    if (r && r->dir == DIR_IN && !r->isRead() && r->conv() == k) n++;
  }
  return n;
}

int MessageLog::totalUnread() {
  int n = 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  for (uint32_t s = lo; s < _next_seq; s++) {
    LogRecord* r = cached(s);
    if (r && r->dir == DIR_IN && !r->isRead()) n++;
  }
  return n;
}

int MessageLog::listConversations(ConvSummary* out, int max) {
  int n = 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  // newest first: the first time we see a conversation is its latest message
  for (uint32_t s = _next_seq; s-- > lo; ) {
    LogRecord* r = cached(s);
    if (!r || (r->flags & LOG_FLAG_SILENT)) continue;
    ConvKey k = r->conv();
    int i = 0;
    for (; i < n; i++) if (out[i].key == k) break;
    if (i == n) {
      if (n >= max) continue;
      ConvSummary& c = out[n++];
      memset(&c, 0, sizeof(c));
      c.key = k;
      c.last_seq = r->seq;
      c.last_time = r->stored_at;
      c.last_dir = r->dir;
      c.last_status = r->status;
      if (r->kind == KIND_CHANNEL && r->dir == DIR_IN && r->name[0]) {
        snprintf(c.preview, sizeof(c.preview), "%s: %s", r->name, r->text);
      } else {
        strncpy(c.preview, r->text, sizeof(c.preview) - 1);
      }
      if (r->kind == KIND_DIRECT) strncpy(c.name, r->name, sizeof(c.name) - 1);
    }
    if (r->dir == DIR_IN && !r->isRead()) out[i].unread++;
  }
  return n;
}

int MessageLog::conversationSeqs(const ConvKey& k, uint32_t* seqs, int max) {
  // collect newest-first, then reverse
  int n = 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  for (uint32_t s = _next_seq; s-- > lo && n < max; ) {
    LogRecord* r = cached(s);
    if (r && r->conv() == k) seqs[n++] = s;
  }
  for (int i = 0; i < n / 2; i++) { uint32_t t = seqs[i]; seqs[i] = seqs[n - 1 - i]; seqs[n - 1 - i] = t; }
  return n;
}

int MessageLog::search(const char* q, uint32_t* seqs, int max) {
  int n = 0;
  if (!q || !*q || !_cache_n) return 0;
  uint32_t lo = _next_seq > _cache_n ? _next_seq - _cache_n : 0;
  for (uint32_t s = _next_seq; s-- > lo && n < max; ) {
    LogRecord* r = cached(s);
    if (!r || (r->flags & LOG_FLAG_SILENT)) continue;
    if (textMatches(r->text, q) || textMatches(r->name, q)) seqs[n++] = s;
  }
  return n;
}
