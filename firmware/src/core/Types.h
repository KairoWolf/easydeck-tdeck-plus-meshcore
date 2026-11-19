#pragma once
// Platform-neutral types shared by the firmware and the desktop UI simulator.
// No Arduino / ESP-IDF headers here.
#include <stdint.h>
#include <string.h>

#define LOG_TEXT_MAX      160   // == MeshCore MAX_TEXT_LEN
#define LOG_NAME_MAX      32
#define LOG_KEY_LEN       6

enum LogKind : uint8_t { KIND_DIRECT = 0, KIND_CHANNEL = 1 };
enum LogDir : uint8_t { DIR_IN = 0, DIR_OUT = 1 };
enum LogOrigin : uint8_t { ORIGIN_RADIO = 0, ORIGIN_DEVICE = 1, ORIGIN_PHONE = 2 };
enum LogStatus : uint8_t {
  ST_NONE = 0,        // incoming messages
  ST_PENDING = 1,     // outgoing direct message, waiting for "delivered" confirmation
  ST_SENT = 2,        // outgoing channel message (channels have no delivery receipts)
  ST_DELIVERED = 3,   // outgoing direct message confirmed by recipient
  ST_FAILED = 4       // outgoing direct message never confirmed
};
#define LOG_FLAG_READ  0x01
#define LOG_FLAG_SOS   0x02   // message is an SOS / emergency message
#define LOG_FLAG_SILENT 0x04  // machine message (location beacon): kept, but hidden from chat views

// Identifies a conversation: a contact (pubkey prefix) or a channel (secret prefix).
struct ConvKey {
  uint8_t kind;
  uint8_t id[LOG_KEY_LEN];
  bool operator==(const ConvKey& o) const { return kind == o.kind && memcmp(id, o.id, LOG_KEY_LEN) == 0; }
  bool operator!=(const ConvKey& o) const { return !(*this == o); }
  static ConvKey direct(const uint8_t* pubkey) { ConvKey k; k.kind = KIND_DIRECT; memcpy(k.id, pubkey, LOG_KEY_LEN); return k; }
  static ConvKey channel(const uint8_t* secret) { ConvKey k; k.kind = KIND_CHANNEL; memcpy(k.id, secret, LOG_KEY_LEN); return k; }
};

struct __attribute__((packed)) LogRecord {
  uint32_t magic;        // LOG_MAGIC when slot is valid
  uint32_t seq;
  uint32_t stored_at;    // our RTC time when stored
  uint32_t sender_ts;    // timestamp from the message itself
  uint32_t ack;          // expected ACK hash (outgoing direct)
  uint8_t  kind;         // LogKind
  uint8_t  dir;          // LogDir
  uint8_t  status;       // LogStatus
  uint8_t  origin;       // LogOrigin
  uint8_t  key[LOG_KEY_LEN];   // contact pubkey prefix, or channel secret prefix
  uint8_t  channel_idx;  // channel slot at time of storing (informational)
  uint8_t  attempts;     // send attempts so far (outgoing direct)
  int8_t   snr4;         // SNR*4 (incoming)
  uint8_t  path_len;     // hops (incoming), 0xFF = direct
  uint8_t  flags;        // LOG_FLAG_*
  uint8_t  text_len;
  char     name[LOG_NAME_MAX];   // channel: sender name; direct: contact name snapshot
  char     text[LOG_TEXT_MAX + 1];
  // repeaters: incoming = the path it came through; outgoing channel message = repeaters heard
  // re-sending it. Hashes of rpt_hsize bytes each (1-3), packed. Zero in records from before 0.52.
  uint8_t  rpt_n;
  uint8_t  rpt_hsize;
  uint8_t  rpt[16];
  uint8_t  _pad[256 - 4*5 - 4 - LOG_KEY_LEN - 6 - LOG_NAME_MAX - (LOG_TEXT_MAX + 1) - 18];

  ConvKey conv() const { ConvKey k; k.kind = kind; memcpy(k.id, key, LOG_KEY_LEN); return k; }
  bool isRead() const { return flags & LOG_FLAG_READ; }
  // add one repeater hash (no duplicates); false when full or a different hash size
  bool addRepeater(const uint8_t* h, uint8_t hsize) {
    if (hsize < 1 || hsize > 3) return false;
    if (rpt_n && rpt_hsize != hsize) return false;
    for (int i = 0; i < rpt_n; i++) if (memcmp(&rpt[i * hsize], h, hsize) == 0) return false;
    if ((rpt_n + 1) * hsize > (int)sizeof(rpt)) return false;
    memcpy(&rpt[rpt_n * hsize], h, hsize);
    rpt_hsize = hsize; rpt_n++;
    return true;
  }
};
static_assert(sizeof(LogRecord) == 256, "LogRecord must be 256 bytes");

struct ConvSummary {
  ConvKey key;
  uint32_t last_seq;
  uint32_t last_time;
  uint16_t unread;
  uint8_t last_dir;
  uint8_t last_status;
  char name[LOG_NAME_MAX];      // name snapshot from the log (contact may be gone)
  char preview[56];
};

// ---- input ----
enum InputType : uint8_t {
  IN_NONE = 0,
  IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT,   // trackball
  IN_SELECT,                           // trackball click
  IN_BACK,                             // trackball long-press
  IN_CHAR,                             // printable character in .ch
  IN_BACKSPACE,
  IN_ENTER,
  IN_SOS_HOLD,                         // trackball held ~3 s: open SOS from anywhere
  IN_SCREENSHOT,                       // a key pressed while the trackball button is held
};

struct InputEvent {
  InputType type;
  char ch;
};
