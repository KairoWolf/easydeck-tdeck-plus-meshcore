#pragma once
// MessageLog — the single source of truth for chat history.
//
// Every message (received over LoRa, typed on the T-Deck, or sent from the phone
// app) is appended here with a monotonically increasing sequence number. The UI
// reads from it, and the EasyDeck phone app syncs from it ("give me everything
// after seq N"), so the device and phone always show the identical history.
//
// Storage: fixed-size 256-byte records in a ring file. On the SD card the ring
// holds 20000 messages (~5 MB); without an SD card it falls back to internal flash
// with a smaller ring. The most recent records are cached in PSRAM.

#include <Arduino.h>
#include <FS.h>

#include "core/Types.h"

class MessageLog {
public:
  class Observer {
  public:
    virtual void onLogAppended(const LogRecord& r) = 0;
    virtual void onLogUpdated(const LogRecord& r) = 0;
    virtual ~Observer() {}
  };

  bool begin(fs::FS& internal_fs);
  bool usingSD() const { return _on_sd; }

  uint32_t logId() const { return _log_id; }
  uint32_t nextSeq() const { return _next_seq; }
  uint32_t firstSeq() const { return _next_seq > _capacity ? _next_seq - _capacity : 0; }

  // Appends r (seq/magic/stored_at are filled in). Returns the new seq.
  uint32_t append(LogRecord& r);
  bool get(uint32_t seq, LogRecord& out);
  bool update(const LogRecord& r);     // rewrite an existing record in place

  // lookups over the cached (recent) window; return true + record if found
  bool findByAck(uint32_t ack, LogRecord& out);
  bool findOutgoing(const ConvKey& k, uint32_t sender_ts, LogRecord& out);

  int  markConversationRead(const ConvKey& k);   // returns number changed
  int  unreadCount(const ConvKey& k);
  int  totalUnread();

  // Conversations sorted most-recent first (from cached window).
  int  listConversations(ConvSummary* out, int max);
  // Seqs of a conversation's most recent messages, oldest first.
  int  conversationSeqs(const ConvKey& k, uint32_t* seqs, int max);

  void setObserver(int slot, Observer* o) { if (slot >= 0 && slot < 2) _obs[slot] = o; }

  static ConvKey directKey(const uint8_t* pubkey) { return ConvKey::direct(pubkey); }
  static ConvKey channelKey(const uint8_t* secret) { return ConvKey::channel(secret); }

private:
  bool openStore(fs::FS& fs, const char* dir, uint32_t capacity);
  bool writeSlot(const LogRecord& r);
  bool readSlot(uint32_t seq, LogRecord& out);
  void saveMeta();
  void loadCache();
  LogRecord* cached(uint32_t seq);
  void notifyAppended(const LogRecord& r) { for (auto o : _obs) if (o) o->onLogAppended(r); }
  void notifyUpdated(const LogRecord& r) { for (auto o : _obs) if (o) o->onLogUpdated(r); }
public:
  bool inBulk() const { return _bulk; }   // true while markConversationRead() rewrites many records
private:
  bool _bulk = false;

  fs::FS* _fs = nullptr;
  String _data_path, _meta_path;
  bool _on_sd = false;
  uint32_t _capacity = 0;
  uint32_t _log_id = 0;
  uint32_t _next_seq = 0;
  LogRecord* _cache = nullptr;   // PSRAM ring of the most recent records
  uint32_t _cache_n = 0;
  Observer* _obs[2] = { nullptr, nullptr };
};
