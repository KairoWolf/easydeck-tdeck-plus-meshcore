#pragma once
// Automatic backup of EasyDeck's internal data (identity, contacts, groups, settings) to the
// SD card, so a reinstall (e.g. through the SD Launcher, which may wipe our flash partition)
// carries everything over. Only used when the data lives in internal flash; when the data is
// already on the SD card there is nothing to back up.
//
//   SD:/easydeck/backup/<file>   one copy per file, written via <file>.tmp + rename
//
// Restore happens once at boot, before MeshCore loads anything, and only when the internal
// storage is empty (no identity) and the SD card has a backup with an identity.
#include <Arduino.h>

int  backupRestoreIfFresh();       // files restored (0 = nothing done)
void backupLoop(bool enabled);     // call from loop(): checks one file every couple of seconds
uint32_t backupLastMs();           // millis() of the last completed pass (0 = never)
bool backupActive();               // internal storage + SD card present
