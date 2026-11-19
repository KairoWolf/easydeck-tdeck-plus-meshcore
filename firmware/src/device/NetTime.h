#pragma once
// Internet time while Wi-Fi is on anyway (updates, maps, packs): SNTP in the network task, handed to
// the loop task through two words (the loop task owns the clock).
#include <Arduino.h>
void netTimeGrab();                          // network task, Wi-Fi connected: waits up to 4 s
bool netTimeTake(uint32_t& unix_utc_now);    // loop task: true once per new time
