#pragma once
// DeviceUI — glue between MeshCore, the MessageLog, the T-Deck hardware and the
// platform-neutral App: LVGL display/touch drivers, input pumping, and turning
// mesh events into log entries.
#include <AbstractUITask.h>
#include "store/MessageLog.h"

class DeviceUI : public AbstractUITask, public MessageLog::Observer {
public:
  DeviceUI(mesh::MainBoard* board, MultiSerialInterface* serial, MessageLog& log)
    : AbstractUITask(board, serial), _log(log) {}

  void beginDisplay();          // splash, as early as possible
  void begin();                 // after mesh + storage are up
  void showFatal(const char* msg);

  // AbstractUITask
  void notify(UIEventType t = UIEventType::none) override {}
  void loop() override;

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel, const char* text) override;
  void onQueueSizeChanged(int msgcount) override {}
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  bool onUnhandledResponse(const ContactInfo& from, uint32_t tag, const uint8_t* data, uint8_t len) override;

  // MessageLog::Observer
  void onLogAppended(const LogRecord& r) override;
  void onLogUpdated(const LogRecord& r) override;

private:
  MessageLog& _log;
  bool _started = false;
};

extern DeviceUI device_ui;
