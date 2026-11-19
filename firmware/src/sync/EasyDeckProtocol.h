#pragma once
// EasyDeck Sync Protocol (EDS) — extension to the MeshCore companion protocol.
//
// Purpose: give a phone app the SAME complete message history the T-Deck shows,
// including messages typed on the T-Deck itself, with live updates both ways.
//
// Transport: the normal MeshCore companion link (BLE Nordic UART or USB serial),
// one frame per BLE write/notify. All integers little-endian.
//
// Compatibility: stock MeshCore firmware answers unknown commands with
// RESP_CODE_ERR (1) / ERR_CODE_UNSUPPORTED_CMD, so an app can probe with
// EDS_CMD_HELLO and fall back. EasyDeck only sends EDS pushes to a client that
// has sent EDS_CMD_HELLO on the current connection, so official MeshCore apps
// never see them. The stock protocol keeps working unchanged alongside.
//
// Typical app flow on connect:
//   1. CMD_APP_START (stock), CMD_SET_DEVICE_TIME (stock)
//   2. EDS_CMD_HELLO                 -> EDS_RESP_HELLO {log_id, first_seq, next_seq}
//      If log_id differs from what the app stored, the device log was reset:
//      the app clears its cache and re-syncs from first_seq.
//   3. EDS_CMD_GET_SINCE {last_seq+1} -> N x EDS_RESP_RECORD, then EDS_RESP_END
//      (repeat until END.next_seq == app's last_seq+1)
//   4. EDS_CMD_GET_SINCE again for the last ~50 records to refresh delivery/read status.
//   5. Live: EDS_PUSH_NEW (fetch with GET_SINCE) and EDS_PUSH_UPDATE (status/read change).
//   6. Sending from the phone uses the stock CMD_SEND_TXT_MSG / CMD_SEND_CHANNEL_TXT_MSG;
//      the device records it in the log and pushes it back as a normal record.
//   7. Reading a chat on the phone: EDS_CMD_MARK_READ so the T-Deck clears its badge too.
//   8. Optional: drain the stock offline queue with CMD_SYNC_NEXT_MESSAGE and discard
//      (those messages are already in the log).

#define EDS_VERSION              1

// ---- commands (app -> device) ----
#define EDS_CMD_HELLO            0xC0   // [0xC0, app_eds_version]
#define EDS_CMD_GET_SINCE        0xC1   // [0xC1, seq u32, max_count u8 (0 = 32)]
#define EDS_CMD_MARK_READ        0xC2   // [0xC2, kind u8, key[6]]
#define EDS_CMD_GET_RECORD       0xC3   // [0xC3, seq u32]

// ---- responses (device -> app) ----
#define EDS_RESP_HELLO           0xC0   // [0xC0, eds_version, log_id u32, first_seq u32, next_seq u32, flags u8 (bit0 = SD card)]
#define EDS_RESP_RECORD          0xC1   // [0xC1, <record>]  (see layout below)
#define EDS_RESP_END             0xC2   // [0xC2, next_seq u32]
#define EDS_RESP_OK              0xC3   // [0xC3, count u16]

// ---- pushes (device -> app, unsolicited; only after HELLO) ----
#define EDS_PUSH_NEW             0xCE   // [0xCE, next_seq u32]
#define EDS_PUSH_UPDATE          0xCF   // [0xCF, <record>]

// ---- record layout inside EDS_RESP_RECORD / EDS_PUSH_UPDATE ----
//  off len field
//   0   4  seq
//   4   4  stored_at     (device clock, unix secs)
//   8   4  sender_ts     (message timestamp, unix secs)
//  12   1  kind          (0 direct, 1 channel)
//  13   1  dir           (0 in, 1 out)
//  14   1  status        (0 none, 1 pending, 2 sent, 3 delivered, 4 failed)
//  15   1  origin        (0 radio, 1 typed on device, 2 sent from phone)
//  16   6  key           (contact pubkey prefix / channel secret prefix)
//  22   1  channel_idx
//  23   1  flags         (bit0 read)
//  24   1  snr*4 (int8)
//  25   1  path_len
//  26   1  name_len      (N)
//  27   N  name          (UTF-8, not terminated)
//  27+N .. text          (UTF-8, rest of frame, not terminated; may be truncated to fit 176-byte frame)
#define EDS_RECORD_HDR           27
