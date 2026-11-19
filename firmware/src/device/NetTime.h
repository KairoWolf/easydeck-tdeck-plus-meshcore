#pragma once
// Internet time while Wi-Fi is on anyway (updates, maps, packs): SNTP in the network task, handed to
// the loop task through two words (the loop task owns the clock).
#include <Arduino.h>
void netTimeGrab();                          // network task, Wi-Fi connected: waits up to 4 s
bool netTimeTake(uint32_t& unix_utc_now);    // loop task: true once per new time
// Wi-Fi off, done by the LOOP task only after every downloaded piece is on the SD card: stops SNTP,
// disconnects, radio off. (Turning Wi-Fi off from a network task at the end of a download, while
// the loop was still writing the last piece, is where 0.52/0.53 updates went wrong.)
void netWifiOff();
