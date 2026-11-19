#include <Arduino.h>
#include <SPI.h>
#include "target.h"

TDeckPlusBoard board;

// The display, SD card and LoRa radio all share ONE SPI host (the global `SPI`).
// Using separate hosts on the same pins breaks GPIO-matrix routing on the ESP32-S3.
// Everything that touches the bus runs on the main loop task, so no locking is needed
// as long as the display driver finishes its DMA before returning (see hal/Display).
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
