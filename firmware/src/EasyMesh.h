#pragma once
// EasyMesh — stock MeshCore companion (MyMesh) plus an "outbox" for messages typed
// on the T-Deck itself: automatic retries, flood fallback, and delivery tracking.
//
// We override processAck() (virtual in BaseChatMesh) so that ACKs for our own
// messages return the contact — that keeps MeshCore's return-path repair working.

#include "MyMesh.h"
#include "store/MessageLog.h"

class EasyMesh : public MyMesh {
public:
  EasyMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables, DataStore& store)
    : MyMesh(radio, rng, rtc, tables, store) {}

  void attachLog(MessageLog* log) { _log = log; }

  // Send a message typed on the device. Returns the log seq, or -1 on failure.
  int32_t sendFromDevice(const ConvKey& to, const char* text);

  // Find a channel slot by its secret prefix; -1 if none.
  int findChannelByKey(const uint8_t* key6, ChannelDetails* out = nullptr);

  // Contacts/channels helpers for the UI
  ContactInfo* contactByKey(const uint8_t* key6) { return lookupContactByPubKey(key6, LOG_KEY_LEN); }

  void loopOutbox();   // call from main loop

  // Public wrappers around protected helpers the UI needs
  bool removeContactByKey(const uint8_t* key6);
  void advertFlood();                                  // flood advert using the default region scope
  void setDefaultScope(const char* name);              // "" = unscoped
  int  joinHashtagChannel(const char* name);           // returns slot or -1
  int  joinSecretChannel(const char* name, const uint8_t* secret16);
  bool leaveChannelByKey(const uint8_t* key6);
  bool requestTelemetry(const uint8_t* key6);

  // per-channel region scope
  struct ChanScope { uint8_t key[LOG_KEY_LEN]; uint8_t mode; char name[31]; uint8_t tkey[16]; };
  #define MAX_CHAN_SCOPES 16
  void setChannelScope(const uint8_t* key6, int mode, const char* name);
  int  channelScope(const uint8_t* key6, char* name_out32) const;
  void loadChannelScopes();

  // Discover: zero-hop query answered by nearby repeaters/sensors
  struct Discovered { uint8_t prefix[8]; uint8_t type; float snr_them, snr_us; uint32_t at_ms; };
  #define DISC_MAX 16
  bool discover();
  int  discovered(Discovered* out, int max) const;
  uint32_t discoverStartedMs() const { return _disc_ms; }

  // raw receive log for the on-device packet viewer
  struct RxEntry { uint32_t at_ms; float snr, rssi; uint8_t len, header, path_len; };
  #define RX_LOG_SIZE 40
  int rxLog(RxEntry* out, int max) const;

protected:
  using MyMesh::sendFloodScoped;   // keep the other overloads visible
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis = 0) override;
  ContactInfo* processAck(const uint8_t* data) override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  void onControlDataRecv(mesh::Packet* packet) override;
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp,
                           const uint8_t* sender_prefix, const char* text) override;
public:
  // author of the room-server post currently being delivered (valid during onMessageRecv)
  const uint8_t* currentAuthor() const { return _author_valid ? _author : nullptr; }
private:
  uint8_t _author[4] = {0};
  bool _author_valid = false;
protected:

private:
  struct OutboxEntry {
    bool used;
    uint32_t seq;
    uint8_t key[PUB_KEY_SIZE];
    uint32_t timestamp;
    uint8_t attempt;
    uint32_t acks[4];
    unsigned long deadline;
    uint8_t busy;   // times the radio queue was full (packet pool exhausted)
  };
  #define OUTBOX_SIZE 24   // room for an SOS fan-out to every favourite plus normal chats
  #define OUTBOX_MAX_ATTEMPTS 3
  OutboxEntry _outbox[OUTBOX_SIZE] = {};
  RxEntry _rx[RX_LOG_SIZE] = {};
  ChanScope _cscope[MAX_CHAN_SCOPES] = {};
  int _n_cscope = 0;
  void saveChannelScopes();
  Discovered _disc[DISC_MAX] = {};
  int _disc_n = 0;
  uint32_t _disc_tag = 0, _disc_ms = 0;
  int _rx_head = 0, _rx_count = 0;
  MessageLog* _log = nullptr;

  int transmit(OutboxEntry& e);   // 1 sent, 0 radio busy (retry soon), -1 impossible
  void finish(OutboxEntry& e, uint8_t status);
};

extern EasyMesh easy_mesh;
