#pragma once
// After a crash (panic), ESP-IDF has written a core dump to the "coredump" flash partition (if the
// partition table has one). At the next boot we read its summary - task, exception cause and
// address, backtrace - and append it with the firmware version to SD:/easydeck/crash.txt (or the
// internal storage), so the crash can be decoded with the matching firmware.elf. Then the dump is
// erased so it's reported once.
#include <Arduino.h>
void crashCaptureAtBoot();          // after storage (and the SD card) are up
const char* crashSummary();         // "" when there was none this boot; short text for the UI
