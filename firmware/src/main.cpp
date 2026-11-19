// EasyDeck — main entry point.
//
// Boot order matters on the T-Deck Plus:
//   1. peripheral power rail on, all SPI chip-selects high   (board.begin)
//   2. display up so the user sees something immediately     (ui.beginDisplay)
//   3. radio, filesystem, mesh, Bluetooth companion link
//   4. full UI

#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Mesh.h>
#include "EasyMesh.h"

#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include <helpers/ArduinoSerialInterface.h>

#include "sync/SyncInterface.h"
#include "store/MessageLog.h"
#include "store/Storage.h"
#include "device/DeviceUI.h"
#include "device/DeviceModel.h"


MultiSerialInterface interface_manager;

// MeshCore's ArduinoSerialInterface always reports "connected", which would make every
// push go out over USB with no host attached (and show the phone as connected). Only
// count USB as connected when a host is actually there.
class UsbSerialInterface : public ArduinoSerialInterface {
public:
  bool isConnected() const override { return (bool)Serial; }
  size_t writeFrame(const uint8_t src[], size_t len) override {
    // MultiSerialInterface::writeFrame only succeeds if EVERY enabled link accepts the frame, so a
    // link with no host attached must report success (frame dropped) or BLE sync stalls forever.
    if (!(bool)Serial) return len;
    if (Serial.availableForWrite() < (int)len + 3) return 0;   // host attached but busy: retry later
    return ArduinoSerialInterface::writeFrame(src, len);
  }
};
UsbSerialInterface usb_serial_interface;

class BleInterface : public SerialBLEInterface {   // same rule for Bluetooth when no phone is connected
public:
  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (!isConnected()) return len;
    return SerialBLEInterface::writeFrame(src, len);
  }
};
BleInterface bluetooth_interface;

MessageLog message_log;
SyncInterface sync_interface(interface_manager, message_log);

DataStore store(DataFS, rtc_clock);   // DataFS = our flash partition, or /easydeck on SD
StdRNG fast_rng;
SimpleMeshTables tables;
EasyMesh easy_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

DeviceUI device_ui(&board, &interface_manager, message_log);

static void halt(const char* why) {
  device_ui.showFatal(why);
  while (1) delay(1000);
}

void setup() {
  Serial.begin(115200);
  board.begin();
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  device_ui.beginDisplay();          // backlight + "Starting…" splash

  if (!radio_init()) halt("Radio did not start.\nPlease restart the device.");
  fast_rng.begin(radio_driver.getRngSeed());

  sdBegin();
  if (storageBegin() == STORAGE_NONE) {
    String d = "No storage found.\nPlease insert the SD card\nand restart.\n\n" + storageDiagnostics();
    Serial.println(d);
    halt(d.c_str());
  }
  store.begin();
  easy_mesh.begin();
  easy_mesh.loadChannelScopes();

  // Message history lives on the SD card when present, else in internal storage.
  message_log.begin(DataFS);
  message_log.setObserver(0, &sync_interface);
  sync_interface.watchLinks(&bluetooth_interface, &usb_serial_interface);
  easy_mesh.attachLog(&message_log);

  // Bluetooth PIN: a fixed PIN (saved in prefs) is far friendlier than a new
  // random one every boot. Default 123456 until the user changes it.
  uint32_t pin = easy_mesh.getNodePrefs()->ble_pin;
  easy_mesh.setBLEPin(pin ? pin : BLE_PIN_CODE);

  bluetooth_interface.begin(BLE_NAME_PREFIX, easy_mesh.getNodePrefs()->node_name, easy_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);

  // MyMesh talks to the phone through our SyncInterface, which mirrors phone-sent
  // messages into the shared history and answers EasyDeck sync commands.
  easy_mesh.startInterface(sync_interface);
  if (easy_mesh.getNodePrefs()->gps_enabled) board.prepareGps();
  sensors.begin();
  easy_mesh.applyGpsPrefs();

  device_model.begin(&message_log, &interface_manager, &store, &bluetooth_interface);
  device_ui.begin();
  easy_mesh.setListener(&device_ui);

  board.onBootComplete();

  // Watchdog: if the main loop ever stalls (> ~5 s), reboot. An active SOS resumes after reboot.
  enableLoopWDT();
}

void loop() {
  easy_mesh.loop();
  easy_mesh.loopOutbox();
  interface_manager.loop();
  sync_interface.loop();
  sensors.loop();
  device_ui.loop();
  rtc_clock.tick();
  board.loop();
}
