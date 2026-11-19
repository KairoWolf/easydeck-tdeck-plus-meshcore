#pragma once
// SyncInterface — sits between MyMesh (stock MeshCore companion logic) and the real
// phone links (BLE / USB). It is a transparent pass-through for the stock protocol,
// plus it:
//   * records messages the phone sends (CMD_SEND_TXT_MSG / CMD_SEND_CHANNEL_TXT_MSG)
//     into the shared MessageLog, so they appear on the T-Deck screen;
//   * tracks their delivery status from the device's replies;
//   * implements the EasyDeck Sync Protocol (see EasyDeckProtocol.h) so the EasyDeck
//     phone app can mirror the full history, including messages typed on the T-Deck.
// No changes to MeshCore itself are needed.

#include <helpers/BaseSerialInterface.h>
#include <helpers/MultiSerialInterface.h>
#include "store/MessageLog.h"

class SyncInterface : public BaseSerialInterface, public MessageLog::Observer {
public:
  SyncInterface(MultiSerialInterface& inner, MessageLog& log) : _inner(inner), _log(log) {}

  // BaseSerialInterface
  void enable() override { _inner.enable(); }
  void disable() override { _inner.disable(); }
  bool isEnabled() const override { return _inner.isEnabled(); }
  bool isConnected() const override { return _inner.isConnected(); }
  bool isWriteBusy() const override { return _inner.isWriteBusy(); }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  void loop();   // call every main-loop iteration

  bool isEdsClientConnected() const { return _eds_active && _inner.isConnected(); }

  // MessageLog::Observer
  void onLogAppended(const LogRecord& r) override;
  void onLogUpdated(const LogRecord& r) override;

private:
  void handleEds(const uint8_t* frame, size_t len);
  void notePhoneDirectSend(const uint8_t* frame, size_t len);
  void notePhoneChannelSend(const uint8_t* frame, size_t len);
  size_t encodeRecord(uint8_t code, const LogRecord& r, uint8_t* out);
  bool sendNow(const uint8_t* buf, size_t len);
  void sweepPhonePending();

  MultiSerialInterface& _inner;
  MessageLog& _log;

  bool _was_connected = false;
  BaseSerialInterface* _links[2] = { nullptr, nullptr };   // BLE, USB: a new session on either resets EDS
  bool _link_was[2] = { false, false };
public:
  void watchLinks(BaseSerialInterface* ble, BaseSerialInterface* usb) { _links[0] = ble; _links[1] = usb; }
private:
  bool _eds_active = false;

  // GET_SINCE streaming state
  bool _streaming = false;
  uint32_t _stream_seq = 0;
  uint16_t _stream_left = 0;

  // pending pushes
  bool _push_new = false;
  #define EDS_UPDATE_Q 16
  uint32_t _update_q[EDS_UPDATE_Q];
  int _update_q_len = 0;

  // phone send -> waiting for MyMesh's reply (RESP_CODE_SENT / OK / ERR)
  int32_t _awaiting_seq = -1;
  bool _awaiting_is_channel = false;

  // phone-sent direct messages waiting for ACK (phone app does the retries)
  struct PhonePending { uint32_t seq; unsigned long last_attempt; };
  #define PHONE_PENDING_MAX 8
  PhonePending _phone_pending[PHONE_PENDING_MAX];
  int _phone_pending_n = 0;
  unsigned long _next_sweep = 0;
};
