#pragma once
// Where EasyDeck keeps MeshCore's data (identity, contacts, channels, settings).
//
//  1. Our own internal flash partition "edfs" (SPIFFS)        — preferred
//  2. A folder on the SD card: /easydeck/meshcore              — fallback
//     (used when the SD Launcher installed us without the data partition,
//      e.g. when "install SPIFFS?" was answered No)
//
// Either way MeshCore sees one fs::FS ("DataFS"), backed by ESP-IDF's VFS with
// the right mount point. We never format the SD card and only write inside /easydeck.
#include <FS.h>

enum StorageMode : uint8_t { STORAGE_NONE = 0, STORAGE_FLASH = 1, STORAGE_SD = 2 };

extern fs::FS DataFS;

bool sdBegin();                 // mount the SD card (never formats it)
bool sdPresent();
StorageMode storageBegin();     // pick + mount storage for DataFS
StorageMode storageMode();
String storageDiagnostics();
// Safe replacement for MeshCore's formatFileSystem(): only ever erases EasyDeck's own
// partition or EasyDeck's own folder on the SD card.
bool storageFactoryReset();    // partition list etc. for the error screen / serial log

// Crash-safe small records: two copies (path and path + "~") with a generation counter and a
// checksum. A reset in the middle of a write can only damage the copy being written; the other
// (previous) copy still loads. Falls back to the legacy single-file format [magic][data].
bool safeSave(fs::FS& fs, const char* path, uint32_t magic, const void* data, size_t len);
bool safeLoad(fs::FS& fs, const char* path, uint32_t magic, void* data, size_t len);
